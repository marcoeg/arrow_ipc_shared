import mmap
import socket
import pyarrow.ipc as ipc

SOCKET_PATH = "/tmp/memfd_socket"
MAP_SIZE = 4096 * 10

def recv_fd(sock):
    fds = socket.socketpair()
    msg, ancdata, *_ = sock.recvmsg(1, socket.CMSG_LEN(4))
    for cmsg_level, cmsg_type, cmsg_data in ancdata:
        if cmsg_level == socket.SOL_SOCKET and cmsg_type == socket.SCM_RIGHTS:
            return int.from_bytes(cmsg_data[:4], "little")

with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
    client.connect(SOCKET_PATH)
    fd = recv_fd(client)

# Do not use `with` here — manage mmap lifecycle manually
mem = mmap.mmap(fd, MAP_SIZE, access=mmap.ACCESS_READ)
buf = memoryview(mem)

reader = ipc.open_stream(buf)
batch = reader.read_next_batch()
print("Received Arrow RecordBatch:")
print("Schema:", batch.schema)
print("Number of rows:", batch.num_rows)
print("First 5 values:", batch.column(0).to_pylist()[:5])

reader.close()
del batch
del reader
del buf  # remove memoryview
mem.close()   
