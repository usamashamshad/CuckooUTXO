Abstract
Unspent Transaction Output (UTXO) on the Bitcoin blockchain allows for transaction
validation. However, the Bitcoin blockchain completing more than 180 million entries
makes it difficult to memory efficiency and transaction throughput. This thesis suggests
the use of Perfect Cuckoo Filters (PCF), a more advanced probabilistic data structure,
on the Bitcoin blockchain. This enables a high accuracy and quick query response time
whilst cutting down memory usage. Through the use of bijective hashing and compact
bucket-based designs, It achieves the low memory in RAM and staying under a 0.001%
false positive rate and registering no false negatives. The implementation simulated
the Bitcoin framework and performs the equivalent of Bitcoin transactions in insert,
query and delete operations. Utilizing the data from Bitcoin, the performance is tested.
The outcomes show a significant decrease in memory requirements while maintaining
megabyte scale memory requirements and real time invocation. Responsiveness in real
time operations Bitcoin’s UTXO management problem is finally solved. This document
transforms the discussed blockchain framework with a focus on addressing scalability
and performance constraints while boosting the efficiency of blockchain transaction
processing.
