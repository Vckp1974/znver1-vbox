static int rtDirRelBuildFullPath(PRTDIRINTERNAL pThis, char *pszPathDst, size_t cbPathDst, const char *pszRelPath)
RTDECL(int)  RTDirRelFileOpen(RTDIR hDir, const char *pszRelFilename, uint64_t fOpen, PRTFILE phFile)
    PRTDIRINTERNAL pThis = hDir;
RTDECL(int) RTDirRelDirOpen(RTDIR hDir, const char *pszDir, RTDIR *phDir)
    PRTDIRINTERNAL pThis = hDir;
RTDECL(int) RTDirRelDirOpenFiltered(RTDIR hDir, const char *pszDirAndFilter, RTDIRFILTER enmFilter,
                                    uint32_t fFlags, RTDIR *phDir)
    PRTDIRINTERNAL pThis = hDir;
RTDECL(int) RTDirRelDirCreate(RTDIR hDir, const char *pszRelPath, RTFMODE fMode, uint32_t fCreate, RTDIR *phSubDir)
    PRTDIRINTERNAL pThis = hDir;
RTDECL(int) RTDirRelDirRemove(RTDIR hDir, const char *pszRelPath)
    PRTDIRINTERNAL pThis = hDir;
RTDECL(int) RTDirRelPathQueryInfo(RTDIR hDir, const char *pszRelPath, PRTFSOBJINFO pObjInfo,
    PRTDIRINTERNAL pThis = hDir;
RTDECL(int) RTDirRelPathSetMode(RTDIR hDir, const char *pszRelPath, RTFMODE fMode, uint32_t fFlags)
    PRTDIRINTERNAL pThis = hDir;
RTDECL(int) RTDirRelPathSetTimes(RTDIR hDir, const char *pszRelPath, PCRTTIMESPEC pAccessTime, PCRTTIMESPEC pModificationTime,
    PRTDIRINTERNAL pThis = hDir;
RTDECL(int) RTDirRelPathSetOwner(RTDIR hDir, const char *pszRelPath, uint32_t uid, uint32_t gid, uint32_t fFlags)
    PRTDIRINTERNAL pThis = hDir;
RTDECL(int) RTDirRelPathRename(RTDIR hDirSrc, const char *pszSrc, RTDIR hDirDst, const char *pszDst, unsigned fRename)
    PRTDIRINTERNAL pThis = hDirSrc;
    PRTDIRINTERNAL pThat = hDirDst;
RTDECL(int) RTDirRelPathUnlink(RTDIR hDir, const char *pszRelPath, uint32_t fUnlink)
    PRTDIRINTERNAL pThis = hDir;
RTDECL(int) RTDirRelSymlinkCreate(RTDIR hDir, const char *pszSymlink, const char *pszTarget,
    PRTDIRINTERNAL pThis = hDir;
RTDECL(int) RTDirRelSymlinkRead(RTDIR hDir, const char *pszSymlink, char *pszTarget, size_t cbTarget, uint32_t fRead)
    PRTDIRINTERNAL pThis = hDir;