# TCP/IP Networking: Reliability, Flow Control, and UDP

Transmission Control Protocol (TCP) establishes a connection with a three-way handshake: the client sends SYN, the server replies SYN-ACK, and the client acknowledges with ACK. Sequence numbers identify bytes in the stream and acknowledgements confirm received data. TCP provides a reliable, ordered byte stream rather than preserving application message boundaries.

A sliding window allows multiple bytes to be in flight before acknowledgements arrive. The receiver advertises a receive window to provide flow control and avoid overflowing its buffers. Congestion control protects the network: slow start increases the congestion window, while congestion avoidance uses a more cautious growth strategy. Loss signals such as duplicate acknowledgements or retransmission timeouts cause the sender to reduce its sending rate.

UDP is a connectionless datagram protocol with low per-packet overhead. It does not provide built-in retransmission, ordering, or congestion control. Applications such as DNS, real-time media, and some transport protocols build the reliability or timing behavior they need above UDP.

TCP/IP networking therefore involves different concerns: handshake state, retransmission, sliding-window flow control, congestion avoidance, routing, and application framing. A correct network design distinguishes receiver capacity from congestion in the wider network.
