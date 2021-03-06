/* Copyright (C) 2007-2011 Open Information Security Foundation
 *
 * You can copy, redistribute or modify this Program under the terms of
 * the GNU General Public License version 2 as published by the Free
 * Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

/**
 * \file
 *
 * \author Victor Julien <victor@inliniac.net>
 * \author Pablo Rincon <pablo.rincon.crespo@gmail.com>
 *
 */

#include "suricata-common.h"
#include "threads.h"
#include "debug.h"
#include "decode.h"

#include "detect.h"
#include "detect-parse.h"

#include "detect-engine.h"
#include "detect-engine-mpm.h"
#include "detect-engine-state.h"

#include "flow.h"
#include "flow-var.h"
#include "flow-util.h"

#include "util-debug.h"
#include "util-unittest.h"
#include "util-unittest-helper.h"
#include "util-spm-bm.h"
#include "util-print.h"
#include "util-memcmp.h"

#include "app-layer.h"

#include "stream-tcp.h"
#include "detect-fileext.h"

int DetectFileextMatch (ThreadVars *, DetectEngineThreadCtx *, Flow *, uint8_t, void *, Signature *, SigMatch *);
static int DetectFileextSetup (DetectEngineCtx *, Signature *, char *);
void DetectFileextRegisterTests(void);
void DetectFileextFree(void *);

/**
 * \brief Registration function for keyword: fileext
 */
void DetectFileextRegister(void) {
    sigmatch_table[DETECT_FILEEXT].name = "fileext";
    sigmatch_table[DETECT_FILEEXT].Match = NULL;
    sigmatch_table[DETECT_FILEEXT].AppLayerMatch = DetectFileextMatch;
    sigmatch_table[DETECT_FILEEXT].alproto = ALPROTO_HTTP;
    sigmatch_table[DETECT_FILEEXT].Setup = DetectFileextSetup;
    sigmatch_table[DETECT_FILEEXT].Free  = DetectFileextFree;
    sigmatch_table[DETECT_FILEEXT].RegisterTests = DetectFileextRegisterTests;

	SCLogDebug("registering fileext rule option");
    return;
}

/**
 * \brief match the specified file extension
 *
 * \param t pointer to thread vars
 * \param det_ctx pointer to the pattern matcher thread
 * \param p pointer to the current packet
 * \param m pointer to the sigmatch that we will cast into DetectFileextData
 *
 * \retval 0 no match
 * \retval 1 match
 */
int DetectFileextMatch (ThreadVars *t, DetectEngineThreadCtx *det_ctx, Flow *f, uint8_t flags, void *state, Signature *s, SigMatch *m)
{
    SCEnter();
    int ret = 0;

    DetectFileextData *fileext = (DetectFileextData *)m->ctx;
    File *file = (File *)state;

    if (file->name == NULL)
        SCReturnInt(0);

    if (file->txid < det_ctx->tx_id)
        SCReturnInt(0);

    if (file->txid > det_ctx->tx_id)
        SCReturnInt(0);

    if (file->name_len <= fileext->len)
        SCReturnInt(0);

    int offset = file->name_len - fileext->len;

    if (file->name[offset - 1] == '.' &&
        SCMemcmp(file->name + offset, fileext->ext, fileext->len) == 0)
    {
        if (!(fileext->flags & DETECT_CONTENT_NEGATED)) {
            ret = 1;
            SCLogDebug("File ext found");
        }
    }

    if (ret == 0 && fileext->flags & DETECT_CONTENT_NEGATED) {
        SCLogDebug("negated match");
        ret = 1;
    }

    SCReturnInt(ret);
}

/**
 * \brief This function is used to parse fileet
 *
 * \param str Pointer to the fileext value string
 *
 * \retval pointer to DetectFileextData on success
 * \retval NULL on failure
 */
DetectFileextData *DetectFileextParse (char *str)
{
    DetectFileextData *fileext = NULL;

    /* We have a correct filename option */
    fileext = SCMalloc(sizeof(DetectFileextData));
    if (fileext == NULL)
        goto error;

    memset(fileext, 0x00, sizeof(DetectFileextData));

    if (DetectParseContentString (str, &fileext->ext, &fileext->len, &fileext->flags) == -1) {
        goto error;
    }

    SCLogDebug("flags %02X", fileext->flags);
    if (fileext->flags & DETECT_CONTENT_NEGATED) {
        SCLogDebug("negated fileext");
    }

#ifdef DEBUG
    if (SCLogDebugEnabled()) {
        char *ext = SCMalloc(fileext->len + 1);
        memcpy(ext, fileext->ext, fileext->len);
        ext[fileext->len] = '\0';
        SCLogDebug("will look for fileext %s", ext);
    }
#endif

    return fileext;

error:
    if (fileext != NULL)
        DetectFileextFree(fileext);
    return NULL;

}

/**
 * \brief this function is used to add the parsed "id" option
 *        into the current signature
 *
 * \param de_ctx pointer to the Detection Engine Context
 * \param s pointer to the Current Signature
 * \param idstr pointer to the user provided "id" option
 *
 * \retval 0 on Success
 * \retval -1 on Failure
 */
static int DetectFileextSetup (DetectEngineCtx *de_ctx, Signature *s, char *str)
{
    DetectFileextData *fileext= NULL;
    SigMatch *sm = NULL;

    fileext = DetectFileextParse(str);
    if (fileext == NULL)
        goto error;

    /* Okay so far so good, lets get this into a SigMatch
     * and put it in the Signature. */
    sm = SigMatchAlloc();
    if (sm == NULL)
        goto error;

    sm->type = DETECT_FILEEXT;
    sm->ctx = (void *)fileext;

    SigMatchAppendSMToList(s, sm, DETECT_SM_LIST_FILEMATCH);


    if (s->alproto != ALPROTO_UNKNOWN && s->alproto != ALPROTO_HTTP) {
        SCLogError(SC_ERR_CONFLICTING_RULE_KEYWORDS, "rule contains conflicting keywords.");
        goto error;
    }

    AppLayerHtpNeedFileInspection();
    s->alproto = ALPROTO_HTTP;
    s->file_flags |= (FILE_SIG_NEED_FILE|FILE_SIG_NEED_FILENAME);
    return 0;

error:
    if (fileext != NULL)
        DetectFileextFree(fileext);
    if (sm != NULL)
        SCFree(sm);
    return -1;

}

/**
 * \brief this function will free memory associated with DetectFileextData
 *
 * \param fileext pointer to DetectFileextData
 */
void DetectFileextFree(void *ptr) {
    if (ptr != NULL) {
        DetectFileextData *fileext = (DetectFileextData *)ptr;
        if (fileext->ext != NULL)
            SCFree(fileext->ext);
        SCFree(fileext);
    }
}

#ifdef UNITTESTS /* UNITTESTS */

/**
 * \test DetectFileextTestParse01
 */
int DetectFileextTestParse01 (void) {
    DetectFileextData *dfd = DetectFileextParse("doc");
    if (dfd != NULL) {
        DetectFileextFree(dfd);
        return 1;
    }
    return 0;
}

/**
 * \test DetectFileextTestParse02
 */
int DetectFileextTestParse02 (void) {
    int result = 0;

    DetectFileextData *dfd = DetectFileextParse("\"tar.gz\"");
    if (dfd != NULL) {
        if (dfd->len == 6 && memcmp(dfd->ext, "tar.gz", 6) == 0) {
            result = 1;
        }

        DetectFileextFree(dfd);
        return result;
    }
    return 0;
}

/**
 * \test DetectFileextTestParse03
 */
int DetectFileextTestParse03 (void) {
    int result = 0;

    DetectFileextData *dfd = DetectFileextParse("\"pdf\"");
    if (dfd != NULL) {
        if (dfd->len == 3 && memcmp(dfd->ext, "pdf", 3) == 0) {
            result = 1;
        }

        DetectFileextFree(dfd);
        return result;
    }
    return 0;
}

#endif /* UNITTESTS */

/**
 * \brief this function registers unit tests for DetectFileext
 */
void DetectFileextRegisterTests(void) {
#ifdef UNITTESTS /* UNITTESTS */
    UtRegisterTest("DetectFileextTestParse01", DetectFileextTestParse01, 1);
    UtRegisterTest("DetectFileextTestParse02", DetectFileextTestParse02, 1);
    UtRegisterTest("DetectFileextTestParse03", DetectFileextTestParse03, 1);
#endif /* UNITTESTS */
}
