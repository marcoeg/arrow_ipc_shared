# Deep Zero-Copy IPC with Apache Arrow and Shared Memory

This proof of concept demonstrates a high-performance inter-process communication (IPC) mechanism using:

- Apache Arrow for typed, columnar data serialization
- Zero-copy memory sharing with Linux `memfd_create` and `mmap`
- UNIX domain sockets for file descriptor transfer

By combining these tools, this project allows two processes (written in C++ and Python) to share a typed Arrow RecordBatch in memory without making any data copies.


All memory allocation on the C++ side was done via:
- `memfd_create()` → kernel-managed anonymous in-memory file
- `mmap()` → direct virtual memory mapping of the memfd

Apache Arrow’s builders, which internally use Arrow’s memory pool (but no userland malloc().

This is a true zero-copy, heap-free data pipeline.

For detailed memory profiling and validation of zero-copy behavior, see [PROOF.md](PROOF.md).

## Why This Matters

Traditional IPC methods like pipes, message queues, or gRPC often serialize and copy data multiple times across processes. This proof of concept avoids that by:

- Using Arrow's standardized in-memory format  
- Writing directly to a memory file (memfd)  
- Mapping it into another process via `mmap`  
- Transferring file ownership with `SCM_RIGHTS` on UNIX sockets  
- Parsing it in-place in Python with PyArrow

The result is structured, typed tabular data shared with zero serialization overhead.

## Stack

| Component        | Role                                                  |
|------------------|-------------------------------------------------------|
| Apache Arrow     | Typed memory format and IPC stream serialization     |
| C++ (Arrow)      | Creates and serializes the table into shared memory  |
| Python (PyArrow) | Receives, maps, and decodes the memory stream        |
| memfd_create     | Anonymous in-memory file used as a transport         |
| mmap             | Shared-memory mapping of the memfd                   |
| UNIX Sockets     | Used to pass the file descriptor between processes   |

## Project Structure

```
arrow_ipc_shared/
├── sender.cpp        # C++ producer of Arrow table in shared memory
├── receiver.py       # Python consumer via PyArrow and mmap
├── CMakeLists.txt    # For building sender.cpp
└── README.md         # This file
```

## Prerequisites

- Linux (required for `memfd_create`)
- Apache Arrow (C++ and Python bindings)
- CMake 3.14+
- Python 3.10+

Install:

```bash
# Apache Arrow C++
sudo apt install libarrow-dev

# Python bindings
pip install pyarrow
```

## Build and Run

### 1. Build the C++ sender

```bash
mkdir build && cd build
cmake ..
make
```

### 2. Run the sender

```bash
./sender
```

This creates a `memfd`, writes a RecordBatch of random int32s, and sends the file descriptor over a UNIX socket.

### 3. Run the Python receiver (in a second terminal)

```bash
python3 ../receiver.py
```

This maps the `memfd`, reads the stream using PyArrow, and prints the resulting table with pandas.

## Example Output

```
RecordBatch schema: rand: int32
Column name: rand
Column Arrow type: int32
== Arrow Table Summary ==
Schema      : rand: int32
Num Columns : 1
Num Rows    : 100

== First 10 Rows ==
rand
----------------------------------------
0
13
76
46
53
22
4
68
68
94
```

## How It Works (Internally)

1. C++ builds an Arrow RecordBatch, serializes it into an in-memory file (via `FileOutputStream` wrapping a `memfd`).
2. `memfd_create` is a Linux-specific syscall that returns a file descriptor backed by RAM.
3. `dup()` ensures Arrow can close its copy of the file descriptor without affecting later use.
4. The file descriptor is transferred via a UNIX socket using `SCM_RIGHTS`.
5. Python receives the file descriptor, uses `mmap` to map it, and `pyarrow.ipc.open_stream()` to read it in-place.

## Next Steps / Extensions

This is a foundation for fast, shared-memory analytics. Potential extensions:

- Support multiple batches or a stream of tables
- Add C++ receiver to show cross-language symmetry
- Add back-pressure or request-response for larger messages
- Add schema negotiation or versioning
- Turn into a reusable IPC library

## Resources

- [Apache Arrow Format](https://arrow.apache.org/docs/format/Columnar.html)
- [Linux memfd_create](https://man7.org/linux/man-pages/man2/memfd_create.2.html)
- [UNIX Domain Sockets & SCM_RIGHTS](https://man7.org/linux/man-pages/man7/unix.7.html)

## Author & Credits

Made by Marco Graziano and ChatGPT-4, blending systems programming, typed memory, and high-performance data sharing.

## License

MIT License

