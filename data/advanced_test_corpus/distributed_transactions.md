# Distributed Transactions: 2PC, Quorums, CAP, and PACELC

Two-Phase Commit (2PC) coordinates an atomic transaction across multiple participants. In the prepare phase, a coordinator asks each participant to promise that it can commit. Participants record enough information to recover and vote YES or NO. In the commit phase, the coordinator broadcasts COMMIT if every participant voted YES; otherwise it broadcasts ABORT. Classic 2PC can block if the coordinator fails after participants have prepared and before they learn the outcome.

Quorum protocols rely on overlapping read and write sets. If every successful read quorum intersects every successful write quorum, a reader can discover a sufficiently recent value. The exact quorum sizes depend on replica count and failure assumptions. Quorum intersection is a safety property; availability during network partitions is a separate concern.

The CAP theorem states that during a network partition, a distributed data store cannot guarantee both linearizable consistency and availability for every request. PACELC adds that when there is no partition, systems still trade latency against consistency. These are design trade-offs rather than simple product labels.

Transaction protocols, consensus, replication, and isolation solve related but different problems. A system must define failure recovery, idempotency, durable logging, and how clients learn whether an operation committed.
