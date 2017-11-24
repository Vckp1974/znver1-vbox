static int rtDirRelBuildFullPath(PRTDIRINTERNAL pThis, char *pszPathDst, size_t cbPathDst, const char *pszRelPath)
RTDECL(int)  RTDirRelFileOpen(RTDIR hDir, const char *pszRelFilename, uint64_t fOpen, PRTFILE phFile)
    PRTDIRINTERNAL pThis = hDir;
RTDECL(int) RTDirRelDirOpen(RTDIR hDir, const char *pszDir, RTDIR *phDir)
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
    fMode = rtFsModeNormalize(fMode, pszRelPath, 0);
    AssertReturn(rtFsModeIsValidPermissions(fMode), VERR_INVALID_FMODE);
    /*
     * Convert and normalize the path.
     */
    UNICODE_STRING NtName;
    HANDLE hRoot = pThis->hDir;
    int rc = RTNtPathRelativeFromUtf8(&NtName, &hRoot, pszRelPath, RTDIRREL_NT_GET_ASCENT(pThis),
                                      pThis->enmInfoClass == FileMaximumInformation);
        HANDLE              hSubDir = RTNT_INVALID_HANDLE_VALUE;
        IO_STATUS_BLOCK     Ios     = RTNT_IO_STATUS_BLOCK_INITIALIZER;
        OBJECT_ATTRIBUTES   ObjAttr;
        InitializeObjectAttributes(&ObjAttr, &NtName, 0 /*fAttrib*/, hRoot, NULL);

        ULONG fOpenOptions = FILE_OPEN_FOR_BACKUP_INTENT | FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_REPARSE_POINT;
        if (fFlags & RTPATH_F_ON_LINK)
            fOpenOptions |= FILE_OPEN_REPARSE_POINT;
        NTSTATUS rcNt = NtCreateFile(&hSubDir,
                                     FILE_WRITE_ATTRIBUTES | SYNCHRONIZE,
                                     &ObjAttr,
                                     &Ios,
                                     NULL /*AllocationSize*/,
                                     FILE_ATTRIBUTE_NORMAL,
                                     FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE,
                                     FILE_OPEN,
                                     fOpenOptions,
                                     NULL /*EaBuffer*/,
                                     0 /*EaLength*/);
        if (NT_SUCCESS(rcNt))
        {
            rc = rtNtFileSetModeWorker(hSubDir, fMode);

            rcNt = NtClose(hSubDir);
            if (!NT_SUCCESS(rcNt) && RT_SUCCESS(rc))
                rc = RTErrConvertFromNtStatus(rcNt);
        }
        else
            rc = RTErrConvertFromNtStatus(rcNt);

        RTNtPathFree(&NtName, NULL);
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