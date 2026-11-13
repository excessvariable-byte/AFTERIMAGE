# Garbage Collection and Heap Reclamation

Garbage collection reclaims heap objects that an application can no longer reach. Reference counting stores a count of incoming references and frees an object when the count reaches zero. It can reclaim objects promptly, but reference cycles can remain allocated unless the system adds cycle detection or weak references.

Mark-Sweep collection begins from root references such as stack variables, registers, and global objects. The mark phase traverses reachable objects and marks them live. The sweep phase scans the heap and reclaims unmarked objects. Mark-Sweep handles cycles but can introduce pauses and fragmentation.

Generational garbage collectors exploit the observation that many objects die young. The heap is split into generations, commonly a young generation and an older generation. Minor collections focus on young objects, while remembered sets or write barriers track references from older objects into younger regions. Surviving objects may be promoted.

Collector design balances pause time, throughput, memory overhead, fragmentation, and application latency. Concurrent and incremental collectors spread work over time, but require barriers and careful coordination with mutator threads. Automatic heap reclamation does not remove the need to manage external resources such as file descriptors.
