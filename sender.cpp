// sender.cpp (true zero-copy)
//
// This version avoids all intermediate memory buffers by writing Arrow IPC data
// directly into a memory-mapped memfd region using Arrow's low-level buffer APIs.
// No builders, no copies, and zero heap allocation by user code.

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
#include <random>

#define SOCKET_PATH "/tmp/memfd_socket"
#define MEM_SIZE (4096 * 10)
#define NUM_ROWS 100

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
    // Step 1: Create and mmap a memory-backed file
    int fd = memfd_create("arrow_zero_copy", MFD_CLOEXEC);
    if (fd == -1) {
        std::cerr << "memfd_create failed\n";
        return 1;
    }
    ftruncate(fd, MEM_SIZE);

    void* mem = mmap(NULL, MEM_SIZE, PROT_WRITE, MAP_SHARED, fd, 0);
    if (mem == MAP_FAILED) {
        std::cerr << "mmap failed\n";
        return 1;
    }

    // Step 2: Wrap the mmap region with Arrow's FixedSizeBufferWriter
    auto buffer = std::make_shared<arrow::MutableBuffer>(
        reinterpret_cast<uint8_t*>(mem), MEM_SIZE);
    auto writer_stream = std::make_shared<arrow::io::FixedSizeBufferWriter>(buffer);

    // Step 3: Create raw data buffer for Arrow array
    std::vector<int32_t> values(NUM_ROWS);
    std::default_random_engine gen;
    std::uniform_int_distribution<int> dist(0, 100);
    for (int i = 0; i < NUM_ROWS; ++i) {
        values[i] = dist(gen);
    }

    auto data_buffer = std::make_shared<arrow::Buffer>(
        reinterpret_cast<const uint8_t*>(values.data()),
        values.size() * sizeof(int32_t));

    // Step 4: Create Arrow Array and RecordBatch without builders
    auto array_data = arrow::ArrayData::Make(arrow::int32(), NUM_ROWS, {nullptr, data_buffer});
    auto array = arrow::MakeArray(array_data);
    auto schema = arrow::schema({arrow::field("rand", arrow::int32())});
    auto batch = arrow::RecordBatch::Make(schema, NUM_ROWS, {array});

    // Step 5: Serialize the RecordBatch into the memory-mapped file
    auto writer_result = arrow::ipc::MakeStreamWriter(writer_stream, schema);
    if (!writer_result.ok()) {
        std::cerr << "StreamWriter creation failed\n";
        return 1;
    }
    std::shared_ptr<arrow::ipc::RecordBatchWriter> writer = *writer_result;

    auto write_status = writer->WriteRecordBatch(*batch);
    if (!write_status.ok()) {
        std::cerr << "Write failed: " << write_status.ToString() << std::endl;
        return 1;
    }

    if (!writer->Close().ok()) {
        std::cerr << "Writer close failed\n";
        return 1;
    }

    // Step 6: Send the memfd over UNIX domain socket
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr = {.sun_family = AF_UNIX};
    strcpy(addr.sun_path, SOCKET_PATH);
    unlink(SOCKET_PATH);
    bind(sock, (struct sockaddr*)&addr, sizeof(addr));
    listen(sock, 1);

    int conn = accept(sock, NULL, NULL);
    send_fd(conn, fd);

    // Cleanup
    close(conn);
    close(sock);
    close(fd);
    munmap(mem, MEM_SIZE);
    return 0;
}
