// sender.cpp
//
// This program creates an Apache Arrow RecordBatch in memory using C++,
// writes it to an in-memory file (via memfd_create), and sends that file
// descriptor to another process using a UNIX domain socket.
// This enables zero-copy inter-process communication of typed tabular data.
//
// Dependencies: Apache Arrow C++ library, Linux-only syscalls (memfd_create, sendmsg)

#include <arrow/api.h>
#include <arrow/io/api.h>
#include <arrow/ipc/api.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <iostream>
#include <cstring>

#define SOCKET_PATH "/tmp/memfd_socket"
#define MEM_SIZE (4096 * 10)  // size of the shared memory buffer

// Sends a file descriptor over a UNIX domain socket
void send_fd(int socket, int fd) {
    struct msghdr msg = {};
    char buf[CMSG_SPACE(sizeof(fd))] = {0};
    struct iovec io = { .iov_base = (void*)"F", .iov_len = 1 };
    msg.msg_iov = &io;
    msg.msg_iovlen = 1;
    msg.msg_control = buf;
    msg.msg_controllen = sizeof(buf);

    struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(fd));
    memcpy(CMSG_DATA(cmsg), &fd, sizeof(fd));
    msg.msg_controllen = cmsg->cmsg_len;

    sendmsg(socket, &msg, 0);
}

int main() {
    // 1. Create an in-memory file using memfd
    int fd = memfd_create("arrow_mem", MFD_CLOEXEC);
    if (fd == -1) {
        std::cerr << "Failed to create memfd" << std::endl;
        return 1;
    }
    ftruncate(fd, MEM_SIZE);  // Resize the memory file

    // 2. Build a simple Arrow table with one column of random int32 values
    std::shared_ptr<arrow::Array> array;
    arrow::Int32Builder builder;

    for (int i = 0; i < 100; ++i) {
        auto status = builder.Append(rand() % 100);
        if (!status.ok()) {
            std::cerr << "Append failed: " << status.ToString() << std::endl;
            return 1;
        }
    }

    auto finish_status = builder.Finish(&array);
    if (!finish_status.ok()) {
        std::cerr << "Finish failed: " << finish_status.ToString() << std::endl;
        return 1;
    }

    auto schema = arrow::schema({arrow::field("rand", arrow::int32())});
    auto batch = arrow::RecordBatch::Make(schema, array->length(), {array});

    // 3. Write the RecordBatch to the memfd using Arrow's IPC format
    int writer_fd = dup(fd);  // Duplicate fd because Arrow will take ownership
    auto output_result = arrow::io::FileOutputStream::Open(writer_fd);
    if (!output_result.ok()) {
        std::cerr << "Failed to open FileOutputStream: " << output_result.status().ToString() << std::endl;
        return 1;
    }
    std::shared_ptr<arrow::io::FileOutputStream> output = *output_result;

    auto writer_result = arrow::ipc::MakeStreamWriter(output, schema);
    if (!writer_result.ok()) {
        std::cerr << "Failed to create Arrow writer: " << writer_result.status().ToString() << std::endl;
        return 1;
    }
    std::shared_ptr<arrow::ipc::RecordBatchWriter> writer = *writer_result;

    auto write_status = writer->WriteRecordBatch(*batch);
    if (!write_status.ok()) {
        std::cerr << "Failed to write RecordBatch: " << write_status.ToString() << std::endl;
        return 1;
    }

    auto close_status = writer->Close();
    if (!close_status.ok()) {
        std::cerr << "Failed to close writer: " << close_status.ToString() << std::endl;
        return 1;
    }

    // 4. Set up UNIX domain socket and send the memfd to another process
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        std::cerr << "Socket creation failed\n";
        return 1;
    }

    struct sockaddr_un addr = {.sun_family = AF_UNIX};
    std::strcpy(addr.sun_path, SOCKET_PATH);
    unlink(SOCKET_PATH);  // Remove old socket file
    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Socket bind failed\n";
        return 1;
    }

    listen(sock, 1);
    int conn = accept(sock, NULL, NULL);
    if (conn < 0) {
        std::cerr << "Accept failed\n";
        return 1;
    }

    send_fd(conn, fd);

    // 5. Clean up
    close(conn);
    close(sock);
    close(fd);
    return 0;
}
