# Linux Kernel Virtual Filesystem (VFS)

The Linux Virtual Filesystem (VFS) provides a common object model for different filesystem implementations. A superblock represents a mounted filesystem and its global metadata. An inode represents a filesystem object and stores metadata such as ownership, permissions, timestamps, and block mapping. A dentry represents a directory entry that connects a name to an inode. A file object represents an open file description and tracks state such as the current offset and access mode.

Path lookup walks dentries from a mount and directory context. The dentry cache avoids repeatedly resolving the same names. The page cache stores file data in memory and allows buffered reads and writes to reuse cached pages. Filesystems implement operations behind VFS interfaces so applications can use a consistent system-call surface.

Direct I/O attempts to transfer data between user buffers and storage without using the normal page cache path. Alignment restrictions and device behavior can make Direct I/O more complex than buffered I/O. Mixing buffered and direct access requires attention to cache coherency and filesystem guarantees.

The VFS object model, superblock, inode, dentry, and file structures are central to understanding Linux filesystem operations. Page cache behavior and Direct I/O pipelines affect real storage performance.
