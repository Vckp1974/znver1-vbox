/* $Id: regops.c 77561 2019-03-04 19:56:32Z vboxsync $ */
/** @file
 * vboxsf - VBox Linux Shared Folders VFS, regular file inode and file operations.
 */

/*
 * Copyright (C) 2006-2019 Oracle Corporation
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "vfsmod.h"
#include <linux/uio.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 32)
# include <linux/aio.h> /* struct kiocb before 4.1 */
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 12)
# include <linux/buffer_head.h>
#endif
#if LINUX_VERSION_CODE <  KERNEL_VERSION(2, 6, 31) \
 && LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 12)
# include <linux/writeback.h>
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 23) \
 && LINUX_VERSION_CODE <  KERNEL_VERSION(2, 6, 31)
# include <linux/splice.h>
#endif
#include <iprt/err.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 18)
# define SEEK_END 2
#endif


/**
 * Called when an inode is released to unlink all handles that might impossibly
 * still be associated with it.
 *
 * @param   pInodeInfo  The inode which handles to drop.
 */
void vbsf_handle_drop_chain(struct vbsf_inode_info *pInodeInfo)
{
    struct vbsf_handle *pCur, *pNext;
    unsigned long     fSavedFlags;
    SFLOGFLOW(("vbsf_handle_drop_chain: %p\n", pInodeInfo));
    spin_lock_irqsave(&g_SfHandleLock, fSavedFlags);

    RTListForEachSafe(&pInodeInfo->HandleList, pCur, pNext, struct vbsf_handle, Entry) {
        AssertMsg(   (pCur->fFlags & (VBSF_HANDLE_F_MAGIC_MASK | VBSF_HANDLE_F_ON_LIST))
                  ==                 (VBSF_HANDLE_F_MAGIC      | VBSF_HANDLE_F_ON_LIST), ("%p %#x\n", pCur, pCur->fFlags));
        pCur->fFlags |= VBSF_HANDLE_F_ON_LIST;
        RTListNodeRemove(&pCur->Entry);
    }

    spin_unlock_irqrestore(&g_SfHandleLock, fSavedFlags);
}


/**
 * Locates a handle that matches all the flags in @a fFlags.
 *
 * @returns Pointer to handle on success (retained), use vbsf_handle_release() to
 *          release it.  NULL if no suitable handle was found.
 * @param   pInodeInfo  The inode info to search.
 * @param   fFlagsSet   The flags that must be set.
 * @param   fFlagsClear The flags that must be clear.
 */
struct vbsf_handle *vbsf_handle_find(struct vbsf_inode_info *pInodeInfo, uint32_t fFlagsSet, uint32_t fFlagsClear)
{
    struct vbsf_handle *pCur;
    unsigned long     fSavedFlags;
    spin_lock_irqsave(&g_SfHandleLock, fSavedFlags);

    RTListForEach(&pInodeInfo->HandleList, pCur, struct vbsf_handle, Entry) {
        AssertMsg(   (pCur->fFlags & (VBSF_HANDLE_F_MAGIC_MASK | VBSF_HANDLE_F_ON_LIST))
                  ==                 (VBSF_HANDLE_F_MAGIC      | VBSF_HANDLE_F_ON_LIST), ("%p %#x\n", pCur, pCur->fFlags));
        if ((pCur->fFlags & (fFlagsSet | fFlagsClear)) == fFlagsSet) {
            uint32_t cRefs = ASMAtomicIncU32(&pCur->cRefs);
            if (cRefs > 1) {
                spin_unlock_irqrestore(&g_SfHandleLock, fSavedFlags);
                SFLOGFLOW(("vbsf_handle_find: returns %p\n", pCur));
                return pCur;
            }
            /* Oops, already being closed (safe as it's only ever increased here). */
            ASMAtomicDecU32(&pCur->cRefs);
        }
    }

    spin_unlock_irqrestore(&g_SfHandleLock, fSavedFlags);
    SFLOGFLOW(("vbsf_handle_find: returns NULL!\n"));
    return NULL;
}


/**
 * Slow worker for vbsf_handle_release() that does the freeing.
 *
 * @returns 0 (ref count).
 * @param   pHandle         The handle to release.
 * @param   sf_g            The info structure for the shared folder associated
 *                  with the handle.
 * @param   pszCaller       The caller name (for logging failures).
 */
uint32_t vbsf_handle_release_slow(struct vbsf_handle *pHandle, struct vbsf_super_info *sf_g, const char *pszCaller)
{
    int rc;
    unsigned long fSavedFlags;

    SFLOGFLOW(("vbsf_handle_release_slow: %p (%s)\n", pHandle, pszCaller));

    /*
     * Remove from the list.
     */
    spin_lock_irqsave(&g_SfHandleLock, fSavedFlags);

    AssertMsg((pHandle->fFlags & VBSF_HANDLE_F_MAGIC_MASK) == VBSF_HANDLE_F_MAGIC, ("%p %#x\n", pHandle, pHandle->fFlags));
    Assert(pHandle->pInodeInfo);
    Assert(pHandle->pInodeInfo && pHandle->pInodeInfo->u32Magic == SF_INODE_INFO_MAGIC);

    if (pHandle->fFlags & VBSF_HANDLE_F_ON_LIST) {
        pHandle->fFlags &= ~VBSF_HANDLE_F_ON_LIST;
        RTListNodeRemove(&pHandle->Entry);
    }

    spin_unlock_irqrestore(&g_SfHandleLock, fSavedFlags);

    /*
     * Actually destroy it.
     */
    rc = VbglR0SfHostReqCloseSimple(sf_g->map.root, pHandle->hHost);
    if (RT_FAILURE(rc))
        LogFunc(("Caller %s: VbglR0SfHostReqCloseSimple %#RX64 failed with rc=%Rrc\n", pszCaller, pHandle->hHost, rc));
    pHandle->hHost  = SHFL_HANDLE_NIL;
    pHandle->fFlags = VBSF_HANDLE_F_MAGIC_DEAD;
    kfree(pHandle);
    return 0;
}


/**
 * Appends a handle to a handle list.
 *
 * @param   pInodeInfo          The inode to add it to.
 * @param   pHandle             The handle to add.
 */
void vbsf_handle_append(struct vbsf_inode_info *pInodeInfo, struct vbsf_handle *pHandle)
{
#ifdef VBOX_STRICT
    struct vbsf_handle *pCur;
#endif
    unsigned long fSavedFlags;

    SFLOGFLOW(("vbsf_handle_append: %p (to %p)\n", pHandle, pInodeInfo));
    AssertMsg((pHandle->fFlags & (VBSF_HANDLE_F_MAGIC_MASK | VBSF_HANDLE_F_ON_LIST)) == VBSF_HANDLE_F_MAGIC,
              ("%p %#x\n", pHandle, pHandle->fFlags));
    Assert(pInodeInfo->u32Magic == SF_INODE_INFO_MAGIC);

    spin_lock_irqsave(&g_SfHandleLock, fSavedFlags);

    AssertMsg((pHandle->fFlags & (VBSF_HANDLE_F_MAGIC_MASK | VBSF_HANDLE_F_ON_LIST)) == VBSF_HANDLE_F_MAGIC,
          ("%p %#x\n", pHandle, pHandle->fFlags));
#ifdef VBOX_STRICT
    RTListForEach(&pInodeInfo->HandleList, pCur, struct vbsf_handle, Entry) {
        Assert(pCur != pHandle);
        AssertMsg(   (pCur->fFlags & (VBSF_HANDLE_F_MAGIC_MASK | VBSF_HANDLE_F_ON_LIST))
                  ==                  (VBSF_HANDLE_F_MAGIC     | VBSF_HANDLE_F_ON_LIST), ("%p %#x\n", pCur, pCur->fFlags));
    }
    pHandle->pInodeInfo = pInodeInfo;
#endif

    pHandle->fFlags |= VBSF_HANDLE_F_ON_LIST;
    RTListAppend(&pInodeInfo->HandleList, &pHandle->Entry);

    spin_unlock_irqrestore(&g_SfHandleLock, fSavedFlags);
}


#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 23) \
 && LINUX_VERSION_CODE <  KERNEL_VERSION(2, 6, 31)

/*
 * Some pipe stuff we apparently need for 2.6.23-2.6.30.
 */

static void vbsf_free_pipebuf(struct page *kpage)
{
    kunmap(kpage);
    __free_pages(kpage, 0);
}

static void *vbsf_pipe_buf_map(struct pipe_inode_info *pipe, struct pipe_buffer *pipe_buf, int atomic)
{
    return 0;
}

static void vbsf_pipe_buf_get(struct pipe_inode_info *pipe, struct pipe_buffer *pipe_buf)
{
}

static void vbsf_pipe_buf_unmap(struct pipe_inode_info *pipe, struct pipe_buffer *pipe_buf, void *map_data)
{
}

static int vbsf_pipe_buf_steal(struct pipe_inode_info *pipe, struct pipe_buffer *pipe_buf)
{
    return 0;
}

static void vbsf_pipe_buf_release(struct pipe_inode_info *pipe, struct pipe_buffer *pipe_buf)
{
    vbsf_free_pipebuf(pipe_buf->page);
}

static int vbsf_pipe_buf_confirm(struct pipe_inode_info *info, struct pipe_buffer *pipe_buf)
{
    return 0;
}

static struct pipe_buf_operations vbsf_pipe_buf_ops = {
    .can_merge = 0,
    .map = vbsf_pipe_buf_map,
    .unmap = vbsf_pipe_buf_unmap,
    .confirm = vbsf_pipe_buf_confirm,
    .release = vbsf_pipe_buf_release,
    .steal = vbsf_pipe_buf_steal,
    .get = vbsf_pipe_buf_get,
};

static int vbsf_reg_read_aux(const char *caller, struct vbsf_super_info *sf_g, struct vbsf_reg_info *sf_r,
                             void *buf, uint32_t *nread, uint64_t pos)
{
    int rc = VbglR0SfRead(&g_SfClient, &sf_g->map, sf_r->Handle.hHost, pos, nread, buf, false /* already locked? */ );
    if (RT_FAILURE(rc)) {
        LogFunc(("VbglR0SfRead failed. caller=%s, rc=%Rrc\n", caller,
             rc));
        return -EPROTO;
    }
    return 0;
}

# define LOCK_PIPE(pipe)   do { if (pipe->inode) mutex_lock(&pipe->inode->i_mutex); } while (0)
# define UNLOCK_PIPE(pipe) do { if (pipe->inode) mutex_unlock(&pipe->inode->i_mutex); } while (0)

ssize_t vbsf_splice_read(struct file *in, loff_t * poffset, struct pipe_inode_info *pipe, size_t len, unsigned int flags)
{
    size_t bytes_remaining = len;
    loff_t orig_offset = *poffset;
    loff_t offset = orig_offset;
    struct inode *inode = VBSF_GET_F_DENTRY(in)->d_inode;
    struct vbsf_super_info *sf_g = VBSF_GET_SUPER_INFO(inode->i_sb);
    struct vbsf_reg_info *sf_r = in->private_data;
    ssize_t retval;
    struct page *kpage = 0;
    size_t nsent = 0;

/** @todo rig up a FsPerf test for this code  */
    TRACE();
    if (!S_ISREG(inode->i_mode)) {
        LogFunc(("read from non regular file %d\n", inode->i_mode));
        return -EINVAL;
    }
    if (!len) {
        return 0;
    }

    LOCK_PIPE(pipe);

    uint32_t req_size = 0;
    while (bytes_remaining > 0) {
        kpage = alloc_page(GFP_KERNEL);
        if (unlikely(kpage == NULL)) {
            UNLOCK_PIPE(pipe);
            return -ENOMEM;
        }
        req_size = 0;
        uint32_t nread = req_size = (uint32_t) min(bytes_remaining, (size_t) PAGE_SIZE);
        uint32_t chunk = 0;
        void *kbuf = kmap(kpage);
        while (chunk < req_size) {
            retval = vbsf_reg_read_aux(__func__, sf_g, sf_r, kbuf + chunk, &nread, offset);
            if (retval < 0)
                goto err;
            if (nread == 0)
                break;
            chunk += nread;
            offset += nread;
            nread = req_size - chunk;
        }
        if (!pipe->readers) {
            send_sig(SIGPIPE, current, 0);
            retval = -EPIPE;
            goto err;
        }
        if (pipe->nrbufs < PIPE_BUFFERS) {
            struct pipe_buffer *pipebuf = pipe->bufs + ((pipe->curbuf + pipe->nrbufs) & (PIPE_BUFFERS - 1));
            pipebuf->page = kpage;
            pipebuf->ops = &vbsf_pipe_buf_ops;
            pipebuf->len = req_size;
            pipebuf->offset = 0;
            pipebuf->private = 0;
            pipebuf->flags = 0;
            pipe->nrbufs++;
            nsent += req_size;
            bytes_remaining -= req_size;
            if (signal_pending(current))
                break;
        } else {    /* pipe full */

            if (flags & SPLICE_F_NONBLOCK) {
                retval = -EAGAIN;
                goto err;
            }
            vbsf_free_pipebuf(kpage);
            break;
        }
    }
    UNLOCK_PIPE(pipe);
    if (!nsent && signal_pending(current))
        return -ERESTARTSYS;
    *poffset += nsent;
    return offset - orig_offset;

 err:
    UNLOCK_PIPE(pipe);
    vbsf_free_pipebuf(kpage);
    return retval;
}

#endif /* 2.6.23 <= LINUX_VERSION_CODE < 2.6.31 */

/** Wrapper around put_page / page_cache_release.  */
DECLINLINE(void) vbsf_put_page(struct page *pPage)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 6, 0)
    put_page(pPage);
#else
    page_cache_release(pPage);
#endif
}


/** Wrapper around get_page / page_cache_get.  */
DECLINLINE(void) vbsf_get_page(struct page *pPage)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 6, 0)
    get_page(pPage);
#else
    page_cache_get(pPage);
#endif
}


/** Companion to vbsf_lock_user_pages(). */
DECLINLINE(void) vbsf_unlock_user_pages(struct page **papPages, size_t cPages, bool fSetDirty, bool fLockPgHack)
{
    /* We don't mark kernel pages dirty: */
    if (fLockPgHack)
        fSetDirty = false;

    while (cPages-- > 0)
    {
        struct page *pPage = papPages[cPages];
        if (fSetDirty && !PageReserved(pPage))
            SetPageDirty(pPage);
        vbsf_put_page(pPage);
    }
}


/**
 * Catches kernel_read() and kernel_write() calls and works around them.
 *
 * The file_operations::read and file_operations::write callbacks supposedly
 * hands us the user buffers to read into and write out of.  To allow the kernel
 * to read and write without allocating buffers in userland, they kernel_read()
 * and kernel_write() increases the user space address limit before calling us
 * so that copyin/copyout won't reject it.  Our problem is that get_user_pages()
 * works on the userspace address space structures and will not be fooled by an
 * increased addr_limit.
 *
 * This code tries to detect this situation and fake get_user_lock() for the
 * kernel buffer.
 */
static int vbsf_lock_user_pages_failed_check_kernel(uintptr_t uPtrFrom, size_t cPages, bool fWrite, int rcFailed,
                                                    struct page **papPages, bool *pfLockPgHack)
{
    /*
     * Check that this is valid user memory that is actually in the kernel range.
     */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0)
    if (   access_ok((void *)uPtrFrom, cPages << PAGE_SHIFT)
        && uPtrFrom >= USER_DS.seg)
#else
    if (   access_ok(fWrite ? VERIFY_WRITE : VERIFY_READ, (void *)uPtrFrom, cPages << PAGE_SHIFT)
        && uPtrFrom >= USER_DS.seg)
#endif
    {
        uintptr_t const uPtrLast = (uPtrFrom & ~(uintptr_t)PAGE_OFFSET_MASK) + (cPages << PAGE_SHIFT) - 1;
        uint8_t        *pbPage   = (uint8_t *)uPtrLast;
        size_t          iPage    = cPages;

        /*
         * Touch the pages first (paranoia^2).
         */
        if (fWrite) {
            uint8_t volatile *pbProbe = (uint8_t volatile *)uPtrFrom;
            while (iPage-- > 0) {
                *pbProbe = *pbProbe;
                pbProbe += PAGE_SIZE;
            }
        } else {
            uint8_t const *pbProbe = (uint8_t const *)uPtrFrom;
            while (iPage-- > 0) {
                ASMProbeReadByte(pbProbe);
                pbProbe += PAGE_SIZE;
            }
        }

        /*
         * Get the pages.
         * Note! Fixes here probably applies to rtR0MemObjNativeLockKernel as well.
         */
        iPage = cPages;
        if (   uPtrFrom >= (unsigned long)__va(0)
            && uPtrLast <  (unsigned long)high_memory)
        {
            /* The physical page mapping area: */
            while (iPage-- > 0)
            {
                struct page *pPage = papPages[iPage] = virt_to_page(pbPage);
                vbsf_get_page(pPage);
                pbPage -= PAGE_SIZE;
            }
        }
        else
        {
            /* This is vmalloc or some such thing, so go thru page tables: */
            while (iPage-- > 0)
            {
                struct page *pPage = rtR0MemObjLinuxVirtToPage(pbPage);
                if (pPage) {
                    papPages[iPage] = pPage;
                    vbsf_get_page(pPage);
                    pbPage -= PAGE_SIZE;
                } else {
                    while (++iPage < cPages) {
                        pPage = papPages[iPage];
                        vbsf_put_page(pPage);
                    }
                    return rcFailed;
                }
            }
        }
        *pfLockPgHack = true;
        return 0;
    }

    return rcFailed;
}


/** Wrapper around get_user_pages. */
DECLINLINE(int) vbsf_lock_user_pages(uintptr_t uPtrFrom, size_t cPages, bool fWrite, struct page **papPages, bool *pfLockPgHack)
{
# if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 9, 0)
    ssize_t cPagesLocked = get_user_pages_unlocked(uPtrFrom, cPages, papPages,
                               fWrite ? FOLL_WRITE | FOLL_FORCE : FOLL_FORCE);
# elif LINUX_VERSION_CODE >= KERNEL_VERSION(4, 6, 0)
    ssize_t cPagesLocked = get_user_pages_unlocked(uPtrFrom, cPages, fWrite, 1 /*force*/, papPages);
# elif LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0)
    ssize_t cPagesLocked = get_user_pages_unlocked(current, current->mm, uPtrFrom, cPages, fWrite, 1 /*force*/, papPages);
# else
    struct task_struct *pTask = current;
    size_t cPagesLocked;
    down_read(&pTask->mm->mmap_sem);
    cPagesLocked = get_user_pages(current, current->mm, uPtrFrom, cPages, fWrite, 1 /*force*/, papPages, NULL);
    up_read(&pTask->mm->mmap_sem);
# endif
    *pfLockPgHack = false;
    if (cPagesLocked == cPages)
        return 0;

    /*
     * It failed.
     */
    if (cPagesLocked < 0)
        return vbsf_lock_user_pages_failed_check_kernel(uPtrFrom, cPages, fWrite, (int)cPagesLocked, papPages, pfLockPgHack);

    vbsf_unlock_user_pages(papPages, cPagesLocked, false /*fSetDirty*/, false /*fLockPgHack*/);

    /* We could use uPtrFrom + cPagesLocked to get the correct status here... */
    return -EFAULT;
}


/**
 * Read function used when accessing files that are memory mapped.
 *
 * We read from the page cache here to present the a cohertent picture of the
 * the file content.
 */
static ssize_t vbsf_reg_read_mapped(struct file *file, char /*__user*/ *buf, size_t size, loff_t *off)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
    struct iovec    iov = { .iov_base = buf, .iov_len = size };
    struct iov_iter iter;
    struct kiocb    kiocb;
    ssize_t         cbRet;

    init_sync_kiocb(&kiocb, file);
    kiocb.ki_pos = *off;
    iov_iter_init(&iter, READ, &iov, 1, size);

    cbRet = generic_file_read_iter(&kiocb, &iter);

    *off = kiocb.ki_pos;
    return cbRet;

#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 19)
    struct iovec    iov = { .iov_base = buf, .iov_len = size };
    struct kiocb    kiocb;
    ssize_t         cbRet;

    init_sync_kiocb(&kiocb, file);
    kiocb.ki_pos = *off;

    cbRet = generic_file_aio_read(&kiocb, &iov, 1, *off);
    if (cbRet == -EIOCBQUEUED)
        cbRet = wait_on_sync_kiocb(&kiocb);

    *off = kiocb.ki_pos;
    return cbRet;

#else /* 2.6.18 or earlier: */
    return generic_file_read(file, buf, size, off);
#endif
}


/**
 * Fallback case of vbsf_reg_read() that locks the user buffers and let the host
 * write directly to them.
 */
static ssize_t vbsf_reg_read_locking(struct file *file, char /*__user*/ *buf, size_t size, loff_t *off,
                                     struct vbsf_super_info *sf_g, struct vbsf_reg_info *sf_r)
{
    /*
     * Lock pages and execute the read, taking care not to pass the host
     * more than it can handle in one go or more than we care to allocate
     * page arrays for.  The latter limit is set at just short of 32KB due
     * to how the physical heap works.
     */
    struct page        *apPagesStack[16];
    struct page       **papPages     = &apPagesStack[0];
    struct page       **papPagesFree = NULL;
    VBOXSFREADPGLSTREQ *pReq;
    loff_t              offFile      = *off;
    ssize_t             cbRet        = -ENOMEM;
    size_t              cPages       = (((uintptr_t)buf & PAGE_OFFSET_MASK) + size + PAGE_OFFSET_MASK) >> PAGE_SHIFT;
    size_t              cMaxPages    = RT_MIN(RT_MAX(sf_g->cMaxIoPages, 1), cPages);
    bool                fLockPgHack;

    pReq = (VBOXSFREADPGLSTREQ *)VbglR0PhysHeapAlloc(RT_UOFFSETOF_DYN(VBOXSFREADPGLSTREQ, PgLst.aPages[cMaxPages]));
    while (!pReq && cMaxPages > 4) {
        cMaxPages /= 2;
        pReq = (VBOXSFREADPGLSTREQ *)VbglR0PhysHeapAlloc(RT_UOFFSETOF_DYN(VBOXSFREADPGLSTREQ, PgLst.aPages[cMaxPages]));
    }
    if (pReq && cPages > RT_ELEMENTS(apPagesStack))
        papPagesFree = papPages = kmalloc(cMaxPages * sizeof(sizeof(papPages[0])), GFP_KERNEL);
    if (pReq && papPages) {
        cbRet = 0;
        for (;;) {
            /*
             * Figure out how much to process now and lock the user pages.
             */
            int    rc;
            size_t cbChunk = (uintptr_t)buf & PAGE_OFFSET_MASK;
            pReq->PgLst.offFirstPage = (uint16_t)cbChunk;
            cPages  = RT_ALIGN_Z(cbChunk + size, PAGE_SIZE) >> PAGE_SHIFT;
            if (cPages <= cMaxPages)
                cbChunk = size;
            else {
                cPages  = cMaxPages;
                cbChunk = (cMaxPages << PAGE_SHIFT) - cbChunk;
            }

            rc = vbsf_lock_user_pages((uintptr_t)buf, cPages, true /*fWrite*/, papPages, &fLockPgHack);
            if (rc == 0) {
                size_t iPage = cPages;
                while (iPage-- > 0)
                    pReq->PgLst.aPages[iPage] = page_to_phys(papPages[iPage]);
            } else {
                /** @todo may need fallback here for kernel addresses during exec. sigh.   */
                cbRet = rc;
                break;
            }

            /*
             * Issue the request and unlock the pages.
             */
            rc = VbglR0SfHostReqReadPgLst(sf_g->map.root, pReq, sf_r->Handle.hHost, offFile, cbChunk, cPages);

            vbsf_unlock_user_pages(papPages, cPages, true /*fSetDirty*/, fLockPgHack);

            if (RT_SUCCESS(rc)) {
                /*
                 * Success, advance position and buffer.
                 */
                uint32_t cbActual = pReq->Parms.cb32Read.u.value32;
                AssertStmt(cbActual <= cbChunk, cbActual = cbChunk);
                cbRet   += cbActual;
                offFile += cbActual;
                buf      = (uint8_t *)buf + cbActual;
                size    -= cbActual;

                /*
                 * Are we done already?  If so commit the new file offset.
                 */
                if (!size || cbActual < cbChunk) {
                    *off = offFile;
                    break;
                }
            } else if (rc == VERR_NO_MEMORY && cMaxPages > 4) {
                /*
                 * The host probably doesn't have enough heap to handle the
                 * request, reduce the page count and retry.
                 */
                cMaxPages /= 4;
                Assert(cMaxPages > 0);
            } else {
                /*
                 * If we've successfully read stuff, return it rather than
                 * the error.  (Not sure if this is such a great idea...)
                 */
                if (cbRet > 0)
                    *off = offFile;
                else
                    cbRet = -EPROTO;
                break;
            }
        }
    }
    if (papPagesFree)
        kfree(papPages);
    if (pReq)
        VbglR0PhysHeapFree(pReq);
    return cbRet;
}


/**
 * Read from a regular file.
 *
 * @param file          the file
 * @param buf           the buffer
 * @param size          length of the buffer
 * @param off           offset within the file (in/out).
 * @returns the number of read bytes on success, Linux error code otherwise
 */
static ssize_t vbsf_reg_read(struct file *file, char /*__user*/ *buf, size_t size, loff_t *off)
{
    struct inode *inode = VBSF_GET_F_DENTRY(file)->d_inode;
    struct vbsf_super_info *sf_g = VBSF_GET_SUPER_INFO(inode->i_sb);
    struct vbsf_reg_info *sf_r = file->private_data;
    struct address_space *mapping = inode->i_mapping;

    SFLOGFLOW(("vbsf_reg_read: inode=%p file=%p buf=%p size=%#zx off=%#llx\n", inode, file, buf, size, *off));

    if (!S_ISREG(inode->i_mode)) {
        LogFunc(("read from non regular file %d\n", inode->i_mode));
        return -EINVAL;
    }

    /** @todo XXX Check read permission according to inode->i_mode! */

    if (!size)
        return 0;

    /*
     * If there is a mapping and O_DIRECT isn't in effect, we must at a
     * heed dirty pages in the mapping and read from them.  For simplicity
     * though, we just do page cache reading when there are writable
     * mappings around with any kind of pages loaded.
     */
    if (   mapping
        && mapping->nrpages > 0
        && mapping_writably_mapped(mapping)
        && !(file->f_flags & O_DIRECT)
        && 1 /** @todo make this behaviour configurable */ )
        return vbsf_reg_read_mapped(file, buf, size, off);

    /*
     * For small requests, try use an embedded buffer provided we get a heap block
     * that does not cross page boundraries (see host code).
     */
    if (size <= PAGE_SIZE / 4 * 3 - RT_UOFFSETOF(VBOXSFREADEMBEDDEDREQ, abData[0]) /* see allocator */) {
        uint32_t const         cbReq = RT_UOFFSETOF(VBOXSFREADEMBEDDEDREQ, abData[0]) + size;
        VBOXSFREADEMBEDDEDREQ *pReq  = (VBOXSFREADEMBEDDEDREQ *)VbglR0PhysHeapAlloc(cbReq);
        if (   pReq
            && (PAGE_SIZE - ((uintptr_t)pReq & PAGE_OFFSET_MASK)) >= cbReq) {
            ssize_t cbRet;
            int vrc = VbglR0SfHostReqReadEmbedded(sf_g->map.root, pReq, sf_r->Handle.hHost, *off, (uint32_t)size);
            if (RT_SUCCESS(vrc)) {
                cbRet = pReq->Parms.cb32Read.u.value32;
                AssertStmt(cbRet <= (ssize_t)size, cbRet = size);
                if (copy_to_user(buf, pReq->abData, cbRet) == 0)
                    *off += cbRet;
                else
                    cbRet = -EFAULT;
            } else
                cbRet = -EPROTO;
            VbglR0PhysHeapFree(pReq);
            return cbRet;
        }
        if (pReq)
            VbglR0PhysHeapFree(pReq);
    }

#if 0 /* Turns out this is slightly slower than locking the pages even for 4KB reads (4.19/amd64). */
    /*
     * For medium sized requests try use a bounce buffer.
     */
    if (size <= _64K /** @todo make this configurable? */) {
        void *pvBounce = kmalloc(size, GFP_KERNEL);
        if (pvBounce) {
            VBOXSFREADPGLSTREQ *pReq = (VBOXSFREADPGLSTREQ *)VbglR0PhysHeapAlloc(sizeof(*pReq));
            if (pReq) {
                ssize_t cbRet;
                int vrc = VbglR0SfHostReqReadContig(sf_g->map.root, pReq, sf_r->Handle.hHost, *off,
                                                    (uint32_t)size, pvBounce, virt_to_phys(pvBounce));
                if (RT_SUCCESS(vrc)) {
                    cbRet = pReq->Parms.cb32Read.u.value32;
                    AssertStmt(cbRet <= (ssize_t)size, cbRet = size);
                    if (copy_to_user(buf, pvBounce, cbRet) == 0)
                        *off += cbRet;
                    else
                        cbRet = -EFAULT;
                } else
                    cbRet = -EPROTO;
                VbglR0PhysHeapFree(pReq);
                kfree(pvBounce);
                return cbRet;
            }
            kfree(pvBounce);
        }
    }
#endif

    return vbsf_reg_read_locking(file, buf, size, off, sf_g, sf_r);
}


/**
 * Wrapper around invalidate_mapping_pages() for page cache invalidation so that
 * the changes written via vbsf_reg_write are made visible to mmap users.
 */
DECLINLINE(void) vbsf_reg_write_invalidate_mapping_range(struct address_space *mapping, loff_t offStart, loff_t offEnd)
{
    /*
     * Only bother with this if the mapping has any pages in it.
     *
     * Note! According to the docs, the last parameter, end, is inclusive (we
     *       would have named it 'last' to indicate this).
     *
     * Note! The pre-2.6.12 function might not do enough to sure consistency
     *       when any of the pages in the range is already mapped.
     */
# if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 12)
    if (mapping)
        invalidate_inode_pages2_range(mapping, offStart >> PAGE_SHIFT, (offEnd - 1) >> PAGE_SHIFT);
# elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 60)
    if (mapping && mapping->nrpages > 0)
        invalidate_mapping_pages(mapping, offStart >> PAGE_SHIFT, (offEnd - 1) >> PAGE_SHIFT);
# else
    /** @todo ... */
    RT_NOREF(mapping, offStart, offEnd);
# endif
}


/**
 * Fallback case of vbsf_reg_write() that locks the user buffers and let the host
 * write directly to them.
 */
static ssize_t vbsf_reg_write_locking(struct file *file, const char /*__user*/ *buf, size_t size, loff_t *off, loff_t offFile,
                                      struct inode *inode, struct vbsf_inode_info *sf_i,
                                      struct vbsf_super_info *sf_g, struct vbsf_reg_info *sf_r)
{
    /*
     * Lock pages and execute the write, taking care not to pass the host
     * more than it can handle in one go or more than we care to allocate
     * page arrays for.  The latter limit is set at just short of 32KB due
     * to how the physical heap works.
     */
    struct page         *apPagesStack[16];
    struct page        **papPages     = &apPagesStack[0];
    struct page        **papPagesFree = NULL;
    VBOXSFWRITEPGLSTREQ *pReq;
    ssize_t              cbRet        = -ENOMEM;
    size_t               cPages       = (((uintptr_t)buf & PAGE_OFFSET_MASK) + size + PAGE_OFFSET_MASK) >> PAGE_SHIFT;
    size_t               cMaxPages    = RT_MIN(RT_MAX(sf_g->cMaxIoPages, 1), cPages);
    bool                 fLockPgHack;

    pReq = (VBOXSFWRITEPGLSTREQ *)VbglR0PhysHeapAlloc(RT_UOFFSETOF_DYN(VBOXSFWRITEPGLSTREQ, PgLst.aPages[cMaxPages]));
    while (!pReq && cMaxPages > 4) {
        cMaxPages /= 2;
        pReq = (VBOXSFWRITEPGLSTREQ *)VbglR0PhysHeapAlloc(RT_UOFFSETOF_DYN(VBOXSFWRITEPGLSTREQ, PgLst.aPages[cMaxPages]));
    }
    if (pReq && cPages > RT_ELEMENTS(apPagesStack))
        papPagesFree = papPages = kmalloc(cMaxPages * sizeof(sizeof(papPages[0])), GFP_KERNEL);
    if (pReq && papPages) {
        cbRet = 0;
        for (;;) {
            /*
             * Figure out how much to process now and lock the user pages.
             */
            int    rc;
            size_t cbChunk = (uintptr_t)buf & PAGE_OFFSET_MASK;
            pReq->PgLst.offFirstPage = (uint16_t)cbChunk;
            cPages  = RT_ALIGN_Z(cbChunk + size, PAGE_SIZE) >> PAGE_SHIFT;
            if (cPages <= cMaxPages)
                cbChunk = size;
            else {
                cPages  = cMaxPages;
                cbChunk = (cMaxPages << PAGE_SHIFT) - cbChunk;
            }

            rc = vbsf_lock_user_pages((uintptr_t)buf, cPages, false /*fWrite*/, papPages, &fLockPgHack);
            if (rc == 0) {
                size_t iPage = cPages;
                while (iPage-- > 0)
                    pReq->PgLst.aPages[iPage] = page_to_phys(papPages[iPage]);
            } else {
                cbRet = rc;
                break;
            }

            /*
             * Issue the request and unlock the pages.
             */
            rc = VbglR0SfHostReqWritePgLst(sf_g->map.root, pReq, sf_r->Handle.hHost, offFile, cbChunk, cPages);

            vbsf_unlock_user_pages(papPages, cPages, false /*fSetDirty*/, fLockPgHack);

            if (RT_SUCCESS(rc)) {
                /*
                 * Success, advance position and buffer.
                 */
                uint32_t cbActual = pReq->Parms.cb32Write.u.value32;
                AssertStmt(cbActual <= cbChunk, cbActual = cbChunk);
                cbRet   += cbActual;
                offFile += cbActual;
                buf      = (uint8_t *)buf + cbActual;
                size    -= cbActual;
                if (offFile > i_size_read(inode))
                    i_size_write(inode, offFile);
                vbsf_reg_write_invalidate_mapping_range(inode->i_mapping, offFile - cbActual, offFile);

                /*
                 * Are we done already?  If so commit the new file offset.
                 */
                if (!size || cbActual < cbChunk) {
                    *off = offFile;
                    break;
                }
            } else if (rc == VERR_NO_MEMORY && cMaxPages > 4) {
                /*
                 * The host probably doesn't have enough heap to handle the
                 * request, reduce the page count and retry.
                 */
                cMaxPages /= 4;
                Assert(cMaxPages > 0);
            } else {
                /*
                 * If we've successfully written stuff, return it rather than
                 * the error.  (Not sure if this is such a great idea...)
                 */
                if (cbRet > 0)
                    *off = offFile;
                else
                    cbRet = -EPROTO;
                break;
            }
            sf_i->force_restat = 1; /* mtime (and size) may have changed */
        }
    }
    if (papPagesFree)
        kfree(papPages);
    if (pReq)
        VbglR0PhysHeapFree(pReq);
    return cbRet;
}


/**
 * Write to a regular file.
 *
 * @param file          the file
 * @param buf           the buffer
 * @param size          length of the buffer
 * @param off           offset within the file
 * @returns the number of written bytes on success, Linux error code otherwise
 */
static ssize_t vbsf_reg_write(struct file *file, const char *buf, size_t size, loff_t * off)
{
    struct inode           *inode   = VBSF_GET_F_DENTRY(file)->d_inode;
    struct vbsf_inode_info *sf_i    = VBSF_GET_INODE_INFO(inode);
    struct vbsf_super_info *sf_g    = VBSF_GET_SUPER_INFO(inode->i_sb);
    struct vbsf_reg_info     *sf_r    = file->private_data;
    struct address_space   *mapping = inode->i_mapping;
    loff_t                  pos;

    SFLOGFLOW(("vbsf_reg_write: inode=%p file=%p buf=%p size=%#zx off=%#llx\n", inode, file, buf, size, *off));
    BUG_ON(!sf_i);
    BUG_ON(!sf_g);
    BUG_ON(!sf_r);

    if (!S_ISREG(inode->i_mode)) {
        LogFunc(("write to non regular file %d\n", inode->i_mode));
        return -EINVAL;
    }

    pos = *off;
    /** @todo This should be handled by the host, it returning the new file
     *        offset when appending.  We may have an outdated i_size value here! */
    if (file->f_flags & O_APPEND)
        pos = i_size_read(inode);

    /** @todo XXX Check write permission according to inode->i_mode! */

    if (!size) {
        if (file->f_flags & O_APPEND)  /** @todo check if this is the consensus behavior... */
            *off = pos;
        return 0;
    }

    /*
     * If there are active writable mappings, coordinate with any
     * pending writes via those.
     */
    if (   mapping
        && mapping->nrpages > 0
        && mapping_writably_mapped(mapping)) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 32)
        int err = filemap_fdatawait_range(mapping, pos, pos + size - 1);
        if (err)
            return err;
#else
        /** @todo ...   */
#endif
    }

    /*
     * For small requests, try use an embedded buffer provided we get a heap block
     * that does not cross page boundraries (see host code).
     */
    if (size <= PAGE_SIZE / 4 * 3 - RT_UOFFSETOF(VBOXSFWRITEEMBEDDEDREQ, abData[0]) /* see allocator */) {
        uint32_t const          cbReq = RT_UOFFSETOF(VBOXSFWRITEEMBEDDEDREQ, abData[0]) + size;
        VBOXSFWRITEEMBEDDEDREQ *pReq  = (VBOXSFWRITEEMBEDDEDREQ *)VbglR0PhysHeapAlloc(cbReq);
        if (   pReq
            && (PAGE_SIZE - ((uintptr_t)pReq & PAGE_OFFSET_MASK)) >= cbReq) {
            ssize_t cbRet;
            if (copy_from_user(pReq->abData, buf, size) == 0) {
                int vrc = VbglR0SfHostReqWriteEmbedded(sf_g->map.root, pReq, sf_r->Handle.hHost,
                                       pos, (uint32_t)size);
                if (RT_SUCCESS(vrc)) {
                    cbRet = pReq->Parms.cb32Write.u.value32;
                    AssertStmt(cbRet <= (ssize_t)size, cbRet = size);
                    pos += cbRet;
                    *off = pos;
                    if (pos > i_size_read(inode))
                        i_size_write(inode, pos);
                    vbsf_reg_write_invalidate_mapping_range(mapping, pos - cbRet, pos);
                } else
                    cbRet = -EPROTO;
                sf_i->force_restat = 1; /* mtime (and size) may have changed */
            } else
                cbRet = -EFAULT;

            VbglR0PhysHeapFree(pReq);
            return cbRet;
        }
        if (pReq)
            VbglR0PhysHeapFree(pReq);
    }

#if 0 /* Turns out this is slightly slower than locking the pages even for 4KB reads (4.19/amd64). */
    /*
     * For medium sized requests try use a bounce buffer.
     */
    if (size <= _64K /** @todo make this configurable? */) {
        void *pvBounce = kmalloc(size, GFP_KERNEL);
        if (pvBounce) {
            if (copy_from_user(pvBounce, buf, size) == 0) {
                VBOXSFWRITEPGLSTREQ *pReq = (VBOXSFWRITEPGLSTREQ *)VbglR0PhysHeapAlloc(sizeof(*pReq));
                if (pReq) {
                    ssize_t cbRet;
                    int vrc = VbglR0SfHostReqWriteContig(sf_g->map.root, pReq, sf_r->handle, pos,
                                         (uint32_t)size, pvBounce, virt_to_phys(pvBounce));
                    if (RT_SUCCESS(vrc)) {
                        cbRet = pReq->Parms.cb32Write.u.value32;
                        AssertStmt(cbRet <= (ssize_t)size, cbRet = size);
                        pos += cbRet;
                        *off = pos;
                        if (pos > i_size_read(inode))
                            i_size_write(inode, pos);
                        vbsf_reg_write_invalidate_mapping_range(mapping, pos - cbRet, pos);
                    } else
                        cbRet = -EPROTO;
                    sf_i->force_restat = 1; /* mtime (and size) may have changed */
                    VbglR0PhysHeapFree(pReq);
                    kfree(pvBounce);
                    return cbRet;
                }
                kfree(pvBounce);
            } else {
                kfree(pvBounce);
                return -EFAULT;
            }
        }
    }
#endif

    return vbsf_reg_write_locking(file, buf, size, off, pos, inode, sf_i, sf_g, sf_r);
}


/**
 * Open a regular file.
 *
 * @param inode         the inode
 * @param file          the file
 * @returns 0 on success, Linux error code otherwise
 */
static int vbsf_reg_open(struct inode *inode, struct file *file)
{
    int rc, rc_linux = 0;
    struct vbsf_super_info *sf_g = VBSF_GET_SUPER_INFO(inode->i_sb);
    struct vbsf_inode_info *sf_i = VBSF_GET_INODE_INFO(inode);
    struct vbsf_reg_info *sf_r;
    struct dentry *dentry = VBSF_GET_F_DENTRY(file);
    VBOXSFCREATEREQ *pReq;

    SFLOGFLOW(("vbsf_reg_open: inode=%p file=%p flags=%#x %s\n", inode, file, file->f_flags, sf_i ? sf_i->path->String.ach : NULL));
    BUG_ON(!sf_g);
    BUG_ON(!sf_i);

    sf_r = kmalloc(sizeof(*sf_r), GFP_KERNEL);
    if (!sf_r) {
        LogRelFunc(("could not allocate reg info\n"));
        return -ENOMEM;
    }

    RTListInit(&sf_r->Handle.Entry);
    sf_r->Handle.cRefs  = 1;
    sf_r->Handle.fFlags = VBSF_HANDLE_F_FILE | VBSF_HANDLE_F_MAGIC;
    sf_r->Handle.hHost  = SHFL_HANDLE_NIL;

    /* Already open? */
    if (sf_i->handle != SHFL_HANDLE_NIL) {
        /*
         * This inode was created with vbsf_create_worker(). Check the CreateFlags:
         * O_CREAT, O_TRUNC: inherent true (file was just created). Not sure
         * about the access flags (SHFL_CF_ACCESS_*).
         */
        sf_i->force_restat = 1;
        sf_r->Handle.hHost = sf_i->handle;
        sf_i->handle = SHFL_HANDLE_NIL;
        file->private_data = sf_r;

        sf_r->Handle.fFlags |= VBSF_HANDLE_F_READ | VBSF_HANDLE_F_WRITE; /** @todo fix  */
        vbsf_handle_append(sf_i, &sf_r->Handle);
        SFLOGFLOW(("vbsf_reg_open: returns 0 (#1) - sf_i=%p hHost=%#llx\n", sf_i, sf_r->Handle.hHost));
        return 0;
    }

    pReq = (VBOXSFCREATEREQ *)VbglR0PhysHeapAlloc(sizeof(*pReq) + sf_i->path->u16Size);
    if (!pReq) {
        kfree(sf_r);
        LogRelFunc(("Failed to allocate a VBOXSFCREATEREQ buffer!\n"));
        return -ENOMEM;
    }
    memcpy(&pReq->StrPath, sf_i->path, SHFLSTRING_HEADER_SIZE + sf_i->path->u16Size);
    RT_ZERO(pReq->CreateParms);
    pReq->CreateParms.Handle = SHFL_HANDLE_NIL;

    /* We check the value of pReq->CreateParms.Handle afterwards to
     * find out if the call succeeded or failed, as the API does not seem
     * to cleanly distinguish error and informational messages.
     *
     * Furthermore, we must set pReq->CreateParms.Handle to SHFL_HANDLE_NIL
     * to make the shared folders host service use our fMode parameter */

    if (file->f_flags & O_CREAT) {
        LogFunc(("O_CREAT set\n"));
        pReq->CreateParms.CreateFlags |= SHFL_CF_ACT_CREATE_IF_NEW;
        /* We ignore O_EXCL, as the Linux kernel seems to call create
           beforehand itself, so O_EXCL should always fail. */
        if (file->f_flags & O_TRUNC) {
            LogFunc(("O_TRUNC set\n"));
            pReq->CreateParms.CreateFlags |= SHFL_CF_ACT_OVERWRITE_IF_EXISTS;
        } else
            pReq->CreateParms.CreateFlags |= SHFL_CF_ACT_OPEN_IF_EXISTS;
    } else {
        pReq->CreateParms.CreateFlags |= SHFL_CF_ACT_FAIL_IF_NEW;
        if (file->f_flags & O_TRUNC) {
            LogFunc(("O_TRUNC set\n"));
            pReq->CreateParms.CreateFlags |= SHFL_CF_ACT_OVERWRITE_IF_EXISTS;
        }
    }

    switch (file->f_flags & O_ACCMODE) {
        case O_RDONLY:
            pReq->CreateParms.CreateFlags |= SHFL_CF_ACCESS_READ;
            sf_r->Handle.fFlags |= VBSF_HANDLE_F_READ;
            break;

        case O_WRONLY:
            pReq->CreateParms.CreateFlags |= SHFL_CF_ACCESS_WRITE;
            sf_r->Handle.fFlags |= VBSF_HANDLE_F_WRITE;
            break;

        case O_RDWR:
            pReq->CreateParms.CreateFlags |= SHFL_CF_ACCESS_READWRITE;
            sf_r->Handle.fFlags |= VBSF_HANDLE_F_READ | VBSF_HANDLE_F_WRITE;
            break;

        default:
            BUG();
    }

    if (file->f_flags & O_APPEND) {
        LogFunc(("O_APPEND set\n"));
        pReq->CreateParms.CreateFlags |= SHFL_CF_ACCESS_APPEND;
        sf_r->Handle.fFlags |= VBSF_HANDLE_F_APPEND;
    }

    pReq->CreateParms.Info.Attr.fMode = inode->i_mode;
    LogFunc(("vbsf_reg_open: calling VbglR0SfHostReqCreate, file %s, flags=%#x, %#x\n",
             sf_i->path->String.utf8, file->f_flags, pReq->CreateParms.CreateFlags));
    rc = VbglR0SfHostReqCreate(sf_g->map.root, pReq);
    if (RT_FAILURE(rc)) {
        LogFunc(("VbglR0SfHostReqCreate failed flags=%d,%#x rc=%Rrc\n", file->f_flags, pReq->CreateParms.CreateFlags, rc));
        kfree(sf_r);
        VbglR0PhysHeapFree(pReq);
        return -RTErrConvertToErrno(rc);
    }

    if (pReq->CreateParms.Handle != SHFL_HANDLE_NIL) {
        vbsf_dentry_chain_increase_ttl(dentry);
        rc_linux = 0;
    } else {
        switch (pReq->CreateParms.Result) {
            case SHFL_PATH_NOT_FOUND:
                rc_linux = -ENOENT;
                break;
            case SHFL_FILE_NOT_FOUND:
                /** @todo sf_dentry_increase_parent_ttl(file->f_dentry); if we can trust it.  */
                rc_linux = -ENOENT;
                break;
            case SHFL_FILE_EXISTS:
                vbsf_dentry_chain_increase_ttl(dentry);
                rc_linux = -EEXIST;
                break;
            default:
                vbsf_dentry_chain_increase_parent_ttl(dentry);
                rc_linux = 0;
                break;
        }
    }

    sf_i->force_restat = 1; /** @todo Why?!? */
    sf_r->Handle.hHost = pReq->CreateParms.Handle;
    file->private_data = sf_r;
    vbsf_handle_append(sf_i, &sf_r->Handle);
    VbglR0PhysHeapFree(pReq);
    SFLOGFLOW(("vbsf_reg_open: returns 0 (#2) - sf_i=%p hHost=%#llx\n", sf_i, sf_r->Handle.hHost));
    return rc_linux;
}


/**
 * Close a regular file.
 *
 * @param inode         the inode
 * @param file          the file
 * @returns 0 on success, Linux error code otherwise
 */
static int vbsf_reg_release(struct inode *inode, struct file *file)
{
    struct vbsf_reg_info *sf_r;
    struct vbsf_super_info *sf_g;
    struct vbsf_inode_info *sf_i = VBSF_GET_INODE_INFO(inode);

    SFLOGFLOW(("vbsf_reg_release: inode=%p file=%p\n", inode, file));
    sf_g = VBSF_GET_SUPER_INFO(inode->i_sb);
    sf_r = file->private_data;

    BUG_ON(!sf_g);
    BUG_ON(!sf_r);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 4, 25)
    /* See the smbfs source (file.c). mmap in particular can cause data to be
     * written to the file after it is closed, which we can't cope with.  We
     * copy and paste the body of filemap_write_and_wait() here as it was not
     * defined before 2.6.6 and not exported until quite a bit later. */
    /* filemap_write_and_wait(inode->i_mapping); */
    if (inode->i_mapping->nrpages
        && filemap_fdatawrite(inode->i_mapping) != -EIO)
        filemap_fdatawait(inode->i_mapping);
#endif

    /* Release sf_r, closing the handle if we're the last user. */
    file->private_data = NULL;
    vbsf_handle_release(&sf_r->Handle, sf_g, "vbsf_reg_release");

    sf_i->handle = SHFL_HANDLE_NIL;
    return 0;
}

/**
 * Wrapper around generic/default seek function that ensures that we've got
 * the up-to-date file size when doing anything relative to EOF.
 *
 * The issue is that the host may extend the file while we weren't looking and
 * if the caller wishes to append data, it may end up overwriting existing data
 * if we operate with a stale size.  So, we always retrieve the file size on EOF
 * relative seeks.
 */
static loff_t vbsf_reg_llseek(struct file *file, loff_t off, int whence)
{
    SFLOGFLOW(("vbsf_reg_llseek: file=%p off=%lld whence=%d\n", file, off, whence));

    switch (whence) {
#ifdef SEEK_HOLE
        case SEEK_HOLE:
        case SEEK_DATA:
#endif
        case SEEK_END: {
            struct vbsf_reg_info *sf_r = file->private_data;
            int rc = vbsf_inode_revalidate_with_handle(VBSF_GET_F_DENTRY(file), sf_r->Handle.hHost,
                                                       true /*fForce*/, false /*fInodeLocked*/);
            if (rc == 0)
                break;
            return rc;
        }
    }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 4, 8)
    return generic_file_llseek(file, off, whence);
#else
    return default_llseek(file, off, whence);
#endif
}

/**
 * Flush region of file - chiefly mmap/msync.
 *
 * We cannot use the noop_fsync / simple_sync_file here as that means
 * msync(,,MS_SYNC) will return before the data hits the host, thereby
 * causing coherency issues with O_DIRECT access to the same file as
 * well as any host interaction with the file.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 1, 0)
static int vbsf_reg_fsync(struct file *file, loff_t start, loff_t end, int datasync)
{
# if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
    return __generic_file_fsync(file, start, end, datasync);
# else
    return generic_file_fsync(file, start, end, datasync);
# endif
}
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35)
static int vbsf_reg_fsync(struct file *file, int datasync)
{
    return generic_file_fsync(file, datasync);
}
#else /* < 2.6.35 */
static int vbsf_reg_fsync(struct file *file, struct dentry *dentry, int datasync)
{
# if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 31)
    return simple_fsync(file, dentry, datasync);
# else
    int rc;
    struct inode *inode = dentry->d_inode;
    AssertReturn(inode, -EINVAL);

    /** @todo What about file_fsync()? (<= 2.5.11) */

#  if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 12)
    rc = sync_mapping_buffers(inode->i_mapping);
    if (   rc == 0
        && (inode->i_state & I_DIRTY)
        && ((inode->i_state & I_DIRTY_DATASYNC) || !datasync)
       ) {
        struct writeback_control wbc = {
            .sync_mode = WB_SYNC_ALL,
            .nr_to_write = 0
        };
        rc = sync_inode(inode, &wbc);
    }
#  else  /* < 2.5.12 */
    rc  = fsync_inode_buffers(inode);
#   if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 4, 10)
    rc |= fsync_inode_data_buffers(inode);
#   endif
    /** @todo probably need to do more here... */
#  endif /* < 2.5.12 */
    return rc;
# endif
}
#endif /* < 2.6.35 */


/**
 * File operations for regular files.
 */
struct file_operations vbsf_reg_fops = {
    .read = vbsf_reg_read,
    .open = vbsf_reg_open,
    .write = vbsf_reg_write,
    .release = vbsf_reg_release,
    .mmap = generic_file_mmap,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0)
# if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 31)
/** @todo This code is known to cause caching of data which should not be
 * cached.  Investigate. */
# if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 23)
    .splice_read = vbsf_splice_read,
# else
    .sendfile = generic_file_sendfile,
# endif
    .aio_read = generic_file_aio_read,
    .aio_write = generic_file_aio_write,
# endif
#endif
    .llseek = vbsf_reg_llseek,
    .fsync = vbsf_reg_fsync,
};

struct inode_operations vbsf_reg_iops = {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 18)
    .getattr = vbsf_inode_getattr,
#else
    .revalidate = vbsf_inode_revalidate,
#endif
    .setattr = vbsf_inode_setattr,
};


#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0)

/**
 * Used to read the content of a page into the page cache.
 *
 * Needed for mmap and reads+writes when the file is mmapped in a
 * shared+writeable fashion.
 */
static int vbsf_readpage(struct file *file, struct page *page)
{
    struct inode *inode = VBSF_GET_F_DENTRY(file)->d_inode;
    int           err;

    SFLOGFLOW(("vbsf_readpage: inode=%p file=%p page=%p off=%#llx\n", inode, file, page, (uint64_t)page->index << PAGE_SHIFT));
    Assert(PageLocked(page));

    if (PageUptodate(page)) {
        unlock_page(page);
        return 0;
    }

    if (!is_bad_inode(inode)) {
        VBOXSFREADPGLSTREQ *pReq = (VBOXSFREADPGLSTREQ *)VbglR0PhysHeapAlloc(sizeof(*pReq));
        if (pReq) {
            struct vbsf_super_info *sf_g = VBSF_GET_SUPER_INFO(inode->i_sb);
            struct vbsf_reg_info   *sf_r = file->private_data;
            uint32_t                cbRead;
            int                     vrc;

            pReq->PgLst.offFirstPage = 0;
            pReq->PgLst.aPages[0]    = page_to_phys(page);
            vrc = VbglR0SfHostReqReadPgLst(sf_g->map.root,
                                           pReq,
                                           sf_r->Handle.hHost,
                                           (uint64_t)page->index << PAGE_SHIFT,
                                           PAGE_SIZE,
                                           1 /*cPages*/);

            cbRead = pReq->Parms.cb32Read.u.value32;
            AssertStmt(cbRead <= PAGE_SIZE, cbRead = PAGE_SIZE);
            VbglR0PhysHeapFree(pReq);

            if (RT_SUCCESS(vrc)) {
                if (cbRead == PAGE_SIZE) {
                    /* likely */
                } else {
                    uint8_t *pbMapped = (uint8_t *)kmap(page);
                    RT_BZERO(&pbMapped[cbRead], PAGE_SIZE - cbRead);
                    kunmap(page);
                    /** @todo truncate the inode file size? */
                }

                flush_dcache_page(page);
                SetPageUptodate(page);
                unlock_page(page);
                return 0;
            }
            err = -RTErrConvertToErrno(vrc);
        } else
            err = -ENOMEM;
    } else
        err = -EIO;
    SetPageError(page);
    unlock_page(page);
    return err;
}


/**
 * Used to write out the content of a dirty page cache page to the host file.
 *
 * Needed for mmap and writes when the file is mmapped in a shared+writeable
 * fashion.
 */
static int vbsf_writepage(struct page *page, struct writeback_control *wbc)
{
    struct address_space   *mapping = page->mapping;
    struct inode           *inode   = mapping->host;
    struct vbsf_inode_info *sf_i    = VBSF_GET_INODE_INFO(inode);
    struct vbsf_handle     *pHandle = vbsf_handle_find(sf_i, VBSF_HANDLE_F_WRITE, VBSF_HANDLE_F_APPEND);
    int                     err;

    SFLOGFLOW(("vbsf_writepage: inode=%p page=%p off=%#llx pHandle=%p (%#llx)\n",
               inode, page,(uint64_t)page->index << PAGE_SHIFT, pHandle, pHandle->hHost));

    if (pHandle) {
        struct vbsf_super_info *sf_g = VBSF_GET_SUPER_INFO(inode->i_sb);
        VBOXSFWRITEPGLSTREQ    *pReq = (VBOXSFWRITEPGLSTREQ *)VbglR0PhysHeapAlloc(sizeof(*pReq));
        if (pReq) {
            uint64_t const cbFile    = i_size_read(inode);
            uint64_t const offInFile = (uint64_t)page->index << PAGE_SHIFT;
            uint32_t const cbToWrite = page->index != (cbFile >> PAGE_SHIFT) ? PAGE_SIZE
                                     : (uint32_t)cbFile & (uint32_t)PAGE_OFFSET_MASK;
            int            vrc;

            pReq->PgLst.offFirstPage = 0;
            pReq->PgLst.aPages[0]    = page_to_phys(page);
            vrc = VbglR0SfHostReqWritePgLst(sf_g->map.root,
                                            pReq,
                                            pHandle->hHost,
                                            offInFile,
                                            cbToWrite,
                                            1 /*cPages*/);
            AssertMsgStmt(pReq->Parms.cb32Write.u.value32 == cbToWrite || RT_FAILURE(vrc), /* lazy bird */
                          ("%#x vs %#x\n", pReq->Parms.cb32Write, cbToWrite),
                          vrc = VERR_WRITE_ERROR);
            VbglR0PhysHeapFree(pReq);

            if (RT_SUCCESS(vrc)) {
                /* Update the inode if we've extended the file. */
                /** @todo is this necessary given the cbToWrite calc above? */
                uint64_t const offEndOfWrite = offInFile + cbToWrite;
                if (   offEndOfWrite > cbFile
                    && offEndOfWrite > i_size_read(inode))
                    i_size_write(inode, offEndOfWrite);

                if (PageError(page))
                    ClearPageError(page);

                err = 0;
            } else {
                ClearPageUptodate(page);
                err = -EPROTO;
            }
        } else
            err = -ENOMEM;
        vbsf_handle_release(pHandle, sf_g, "vbsf_writepage");
    } else {
        static uint64_t volatile s_cCalls = 0;
        if (s_cCalls++ < 16)
            printk("vbsf_writepage: no writable handle for %s..\n", sf_i->path->String.ach);
        err = -EPROTO;
    }
    unlock_page(page);
    return err;
}

# if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 24)
/**
 * Called when writing thru the page cache (which we shouldn't be doing).
 */
int vbsf_write_begin(struct file *file, struct address_space *mapping, loff_t pos,
                     unsigned len, unsigned flags, struct page **pagep, void **fsdata)
{
    /** @todo r=bird: We shouldn't ever get here, should we?  Because we don't use
     *        the page cache for any writes AFAIK.  We could just as well use
     *        simple_write_begin & simple_write_end here if we think we really
     *        need to have non-NULL function pointers in the table... */
    static uint64_t volatile s_cCalls = 0;
    if (s_cCalls++ < 16) {
        printk("vboxsf: Unexpected call to vbsf_write_begin(pos=%#llx len=%#x flags=%#x)! Please report.\n",
               (unsigned long long)pos, len, flags);
        RTLogBackdoorPrintf("vboxsf: Unexpected call to vbsf_write_begin(pos=%#llx len=%#x flags=%#x)!  Please report.\n",
                    (unsigned long long)pos, len, flags);
#  ifdef WARN_ON
        WARN_ON(1);
#  endif
    }
    return simple_write_begin(file, mapping, pos, len, flags, pagep, fsdata);
}
# endif /* KERNEL_VERSION >= 2.6.24 */

# if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 4, 10)
/**
 * This is needed to make open accept O_DIRECT as well as dealing with direct
 * I/O requests if we don't intercept them earlier.
 */
#  if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 7, 0)
static ssize_t vbsf_direct_IO(struct kiocb *iocb, struct iov_iter *iter)
#  elif LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0)
static ssize_t vbsf_direct_IO(struct kiocb *iocb, struct iov_iter *iter, loff_t offset)
#  elif LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
static ssize_t vbsf_direct_IO(int rw, struct kiocb *iocb, struct iov_iter *iter, loff_t offset)
#  elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 6)
static ssize_t vbsf_direct_IO(int rw, struct kiocb *iocb, const struct iovec *iov, loff_t offset, unsigned long nr_segs)
#  elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 55)
static int vbsf_direct_IO(int rw, struct kiocb *iocb, const struct iovec *iov, loff_t offset, unsigned long nr_segs)
#  elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 41)
static int vbsf_direct_IO(int rw, struct file *file, const struct iovec *iov, loff_t offset, unsigned long nr_segs)
#  elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 35)
static int vbsf_direct_IO(int rw, struct inode *inode, const struct iovec *iov, loff_t offset, unsigned long nr_segs)
#  elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 26)
static int vbsf_direct_IO(int rw, struct inode *inode, char *buf, loff_t offset, size_t count)
#  else
static int vbsf_direct_IO(int rw, struct inode *inode, struct kiobuf *, unsigned long, int)
#  endif
{
    TRACE();
    return -EINVAL;
}
# endif

/**
 * Address space (for the page cache) operations for regular files.
 */
struct address_space_operations vbsf_reg_aops = {
    .readpage = vbsf_readpage,
    .writepage = vbsf_writepage,
    /** @todo Need .writepages if we want msync performance...  */
# if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 12)
    .set_page_dirty = __set_page_dirty_buffers,
# endif
# if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 24)
    .write_begin = vbsf_write_begin,
    .write_end = simple_write_end,
# else
    .prepare_write = simple_prepare_write,
    .commit_write = simple_commit_write,
# endif
# if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 4, 10)
    .direct_IO = vbsf_direct_IO,
# endif
};

#endif /* LINUX_VERSION_CODE >= 2.6.0 */

