echo info threads:\n
info threads
echo \nthread backtrace:\n
thread apply all backtrace
echo \ncs_main:\n
print ((CCriticalSection) cs_main)
echo \npwalletMain->cs_wallet:\n
print pwalletMain->cs_wallet
echo \nchainActive->cs_chainLock:\n
print ((CChain)chainActive).cs_chainLock
echo \ncs_vNodes:\n
print ((CCriticalSection) cs_vNodes)
echo \ncsTxInQ:\n
print ((CCriticalSection) csTxInQ)

echo \nchainActive tip:\n
print /x *(chainActive.tip._M_b._M_p)
