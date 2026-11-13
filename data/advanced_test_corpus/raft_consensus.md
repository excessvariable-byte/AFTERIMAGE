# Raft Consensus Protocol

Raft is a consensus algorithm that replicates a state machine across a cluster. Every server is in one of three states: **Follower**, **Candidate**, or **Leader**. A follower becomes a candidate when it has not received a valid heartbeat before its election timeout expires. It increments the current term and requests votes. A candidate that receives votes from a majority becomes the leader for that term.

The leader sends periodic heartbeat messages using empty AppendEntries RPCs. These heartbeats prevent followers from starting unnecessary elections. Election timeouts are randomized to reduce repeated split votes. The leader also accepts client commands, appends them to its replicated log, and sends log entries to followers.

Log replication is safe when the leader commits an entry only after it is stored on a majority and the Raft commitment rules for the current term are satisfied. The `prevLogIndex` and `prevLogTerm` fields ensure that a follower's log matches the leader's prefix before new entries are appended. A follower with conflicting uncommitted entries deletes the conflicting suffix.

Raft separates leader election, log replication, and safety. The key safety properties include election restriction, log matching, leader completeness, and state-machine safety. A committed command must not be lost when leadership changes. Terms act like logical epochs and help reject messages from obsolete leaders.
