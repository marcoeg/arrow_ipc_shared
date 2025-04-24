import mmap
import socket
import pyarrow.ipc as ipc
import pyarrow as pa

SOCKET_PATH = "/tmp/memfd_socket"
MAP_SIZE = 4096 * 10

def recv_fd(sock):
    msg, ancdata, *_ = sock.recvmsg(1, socket.CMSG_LEN(4))
    for cmsg_level, cmsg_type, cmsg_data in ancdata:
        if cmsg_level == socket.SOL_SOCKET and cmsg_type == socket.SCM_RIGHTS:
            return int.from_bytes(cmsg_data[:4], "little")

with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
    client.connect(SOCKET_PATH)
    fd = recv_fd(client)

mem = mmap.mmap(fd, MAP_SIZE, access=mmap.ACCESS_READ)
buf = memoryview(mem)

reader = ipc.open_stream(buf)
batch = reader.read_next_batch()

# Native Arrow inspection, no pandas
print("RecordBatch schema:", batch.schema)
print("Column name:", batch.schema.names[0])
print("Column Arrow type:", batch.column(0).type)

# Convert to Table (still no copy)
table = pa.Table.from_batches([batch])
import pyarrow as pa

# ... after reading `batch` and converting to `table`
print("== Arrow Table Summary ==")
print("Schema      :", table.schema)
print("Num Columns :", table.num_columns)
print("Num Rows    :", table.num_rows)

print("\n== First 10 Rows ==")
col_names = table.column_names
rows = zip(*[table.column(i).to_pylist()[:10] for i in range(table.num_columns)])
print(" | ".join(col_names))
print("-" * 40)
for row in rows:
    print(" | ".join(str(x) for x in row))


reader.close()
del batch, table, buf
#mem.close()
