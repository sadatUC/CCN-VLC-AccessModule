/*
 * Copyright (c) 2014-2015, Xerox Corporation (Xerox) and Palo Alto Research Center, Inc (PARC)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL XEROX OR PARC BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ################################################################################
 * #
 * # PATENT NOTICE
 * #
 * # This software is distributed under the BSD 2-clause License (see LICENSE
 * # file).  This BSD License does not make any patent claims and as such, does
 * # not act as a patent grant.  The purpose of this section is for each contributor
 * # to define their intentions with respect to intellectual property.
 * #
 * # Each contributor to this source code is encouraged to state their patent
 * # claims and licensing mechanisms for any contributions made. At the end of
 * # this section contributors may each make their own statements.  Contributor's
 * # claims and grants only apply to the pieces (source code, programs, text,
 * # media, etc) that they have contributed directly to this software.
 * #
 * # There is no guarantee that this section is complete, up to date or accurate. It
 * # is up to the contributors to maintain their portion of this section and up to
 * # the user of the software to verify any claims herein.
 * #
 * # Do not remove this header notification.  The contents of this section must be
 * # present in all distributions of the software.  You may only modify your own
 * # intellectual property statements.  Please provide contact information.
 *
 * - Palo Alto Research Center, Inc
 * This software distribution does not grant any rights to patents owned by Palo
 * Alto Research Center, Inc (PARC). Rights to these patents are available via
 * various mechanisms. As of January 2016 PARC has committed to FRAND licensing any
 * intellectual property used by its contributions to this software. You may
 * contact PARC at cipo@parc.com for more information or visit http://www.ccnx.org
 */

/*****************************************************************************
 * Preamble
 *****************************************************************************/

#undef DEBUG

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "ccnxVLCUtils.h"

#include <parc/algol/parc_Memory.h>
#include <ccnx/api/ccnx_Portal/ccnx_Portal.h>
#include <ccnx/api/ccnx_Portal/ccnx_PortalRTA.h>

#include <ccnx/common/ccnx_Name.h>
#include <ccnx/common/ccnx_NameSegmentNumber.h>

#include <vlc_common.h>
#include <vlc.h>
#include <vlc_plugin.h>
#include <vlc_access.h>
#include <vlc_url.h>
#include <vlc_threads.h>


/*****************************************************************************
 * Disable internationalization
 *****************************************************************************/
#define _(str) (str)
#define N_(str) (str)
/*****************************************************************************
 * Module descriptor
 *****************************************************************************/

#define SEEKABLE_TEXT N_("CCN streams can seek")
#define SEEKABLE_LONGTEXT N_(               \
"Enable or disable seeking within a CCN stream.")

static int  _CCNxOpen(vlc_object_t *);
static void _CCNxClose(vlc_object_t *);

vlc_module_begin();
    set_shortname(N_("CCNx 1.0"));
    set_description(N_("CCNx 1.0 Stream Access"));
    set_category(CAT_INPUT);
    set_subcategory(SUBCAT_INPUT_ACCESS);

    add_bool("ccn-streams-seekable", true, SEEKABLE_TEXT, SEEKABLE_LONGTEXT, true )

    change_safe();
    set_capability("access", 0);
    add_shortcut("ccn1.0");
    add_shortcut("ccnx1.0");
    set_callbacks(_CCNxOpen, _CCNxClose);
vlc_module_end();

// The prefix of the name that we'll use for our Interests. "ccnx:/cnx/tutorial" is
// what the tutorial_Server listens for, and we're using that to serve our movies.
static const char *_domainPrefix = "ccnx:/ccnx/tutorial"; // because we're using tutorial_Server

/*****************************************************************************
 * Local prototypes
 *****************************************************************************/
struct access_sys_t
{
    CCNxPortal *portal;            // The Portal we'll use for communication
    CCNxName   *interestBaseName;  // A CCNxName that we'll copy and extend when we create Interests.
};


/**
 * Create a CCnxPortalFactory, supplying some default credentials.
 */
static CCNxPortalFactory *
_setupPortalFactory(void)
{
    const char *keystoreName = "vlc-ccnx-keystore";
    const char *keystorePassword = "keystore_password";
    const char *subjectName = keystoreName;

    return ccnxVLCUtils_SetupPortalFactory(keystoreName, keystorePassword, subjectName);
}

/**
 * Given a filename and a desired chunk number, create and return a CCNxInterest with
 * the appropriate name required for retrieving that chunk of that filename.
 * 
 * @param p_access - the VLC access_t structure
 * @param fileName - the name of the file (movie) from which to retrieve blocks
 * @param chunkNum - the number of the desired chunk of the file to retrieve
 * 
 * @return a CCNxInterest instance with a name suitable for retrieving the desired
 *         chunk of the specified filed. This instance must eventually be released
 *         by calling ccnxInterest_Release().
 */

static CCNxInterest *
_createInterestForChunk(access_t *p_access, char *fileName, uint64_t chunkNum)
{
    access_sys_t *p_sys = p_access->p_sys;

    if (p_sys->interestBaseName == NULL) {
        CCNxName *interestName = ccnxName_CreateFromCString(_domainPrefix);

        // Append "fetch"
        PARCBuffer *commandBuffer = parcBuffer_AllocateCString("fetch");
        CCNxNameSegment *commandSegment = ccnxNameSegment_CreateTypeValue(CCNxNameLabelType_NAME, commandBuffer);

        ccnxName_Append(interestName, commandSegment);

        parcBuffer_Release(&commandBuffer);
        ccnxNameSegment_Release(&commandSegment);

        // Append the filename

        // The filename might be a path. E.g. "foo/bar/movie.mpg"
        // We need to make each of those a segment.

        char *segment = strtok(fileName, "/");
        while(segment != NULL) {
            msg_Info(p_access, "_createInterestForChunk segment = %s", segment);
            PARCBuffer *segmentBuf = parcBuffer_AllocateCString(segment);
            CCNxNameSegment *fileNameSegment = ccnxNameSegment_CreateTypeValue(CCNxNameLabelType_NAME, segmentBuf);
            ccnxName_Append(interestName, fileNameSegment);

            parcBuffer_Release(&segmentBuf);
            ccnxNameSegment_Release(&fileNameSegment);

            segment = strtok(NULL, "/");
        }

        p_sys->interestBaseName = interestName;

	char *stringName = ccnxName_ToString(p_sys->interestBaseName);
        msg_Info(p_access, "_createInterestForChunk basename = %s", stringName);
	parcMemory_Deallocate(&stringName);

    }

    // Copy the interestBaseName since we'll be adding a chunk segment.
    CCNxName *interestNameWithChunk = ccnxName_Copy(p_sys->interestBaseName);

    // This is the manual version. Add the chunk number directly.
    CCNxNameSegment *chunkNumberSegment = ccnxNameSegmentNumber_Create(CCNxNameLabelType_CHUNK, chunkNum);
    ccnxName_Append(interestNameWithChunk, chunkNumberSegment);
    ccnxNameSegment_Release(&chunkNumberSegment);

    // And, finally, create an Interest with the new name.

     // Appending Frame Number for Interest

     PARCBuffer *frameBuf = parcBuffer_AllocateCString("F50");

     CCNxNameSegment *frameSegment = ccnxNameSegment_CreateTypeValue(CCNxNameLabelType_NAME, frameBuf);

     ccnxName_Append(interestNameWithChunk, frameSegment);
     parcBuffer_Release(&frameBuf);
     ccnxNameSegment_Release(&frameSegment);

     // Appending number of layers for Interest
     PARCBuffer *layerBuf = parcBuffer_AllocateCString("L4");

     CCNxNameSegment *layerSegment = ccnxNameSegment_CreateTypeValue(CCNxNameLabelType_NAME, layerBuf);

     ccnxName_Append(interestNameWithChunk, layerSegment);
     parcBuffer_Release(&layerBuf);
     ccnxNameSegment_Release(&layerSegment);


    CCNxInterest *result = ccnxInterest_CreateSimple(interestNameWithChunk);

    ccnxName_Release(&interestNameWithChunk);

    return result;
}


/**
 * Given the position handed to us by VLC when _ccnxBlock() is called, figure
 * out which chunk contains it. It looks like the position is just a byte count
 * so we just divide by our chunkSize to figure out which chunk contains it.
 * 
 * @param position - a position (byte offset) of the movie 
 * @param chunkSize - the size (in bytes) of our content chunks
 *
 * @return - the chunk number corresponding to the positiong offset
 */
static uint64_t 
_calculateChunkForPosition(uint64_t position, uint64_t chunkSize)
{
    return position / chunkSize;
}

/**
 * Helper function to create and return a VLC block_t containing the requested
 * data at position `position`, extracted from the supplied ContentObject.
 *
 * @param p_access the VLC access structure
 * @param contentObject the CCNxContentObject instance from which to extract the data
 * @param chunkSize the size of the chunks being transferred
 * @param chunkNum the chunk number of the supplied CCNxContentObject
 * @param position the position of the requested data (from VLC)
 * @param [out] payloadSize the size of the payload that was in the ContentObject
 *
 * @return a block_t containing the requested data, or NULL
 */
static block_t *
_extractRequestedBlock(access_t *p_access, 
                       CCNxContentObject *contentObject, 
                       uint64_t chunkSize, uint64_t chunkNum, 
                       uint64_t position, size_t *payloadSize)
{
    block_t *result = NULL;

    PARCBuffer *payload = ccnxContentObject_GetPayload(contentObject);
    *payloadSize = parcBuffer_Remaining(payload);

    u_char *rawPayload = parcBuffer_Overlay(payload, 0);

    size_t startOffset = position % chunkSize;
    size_t numBytesToCopy = *payloadSize - startOffset;
    if (numBytesToCopy > 0) {
        result = block_Alloc(numBytesToCopy);

        memcpy(result->p_buffer, rawPayload + startOffset, numBytesToCopy);
        result->i_size = numBytesToCopy;

        msg_Info(p_access, "Adding %ld bytes from chunk %ld\n", numBytesToCopy, chunkNum);
    }

    return result;
}

/*****************************************************************************
 * _CCNxBlock: Apparently called when VLC needs a block of data.
 *****************************************************************************/
static block_t *
_CCNxBlock(access_t *p_access)
{
    access_sys_t *p_sys = p_access->p_sys;
    block_t *p_block = NULL;

    static uint64_t _lastSeenChunkSize = 1200;

    if (p_access->info.b_eof) {
        msg_Info(p_access, "_CCNxBlock EOF");
    }

#ifdef DEBUG
    msg_Info(p_access, "_CCNxBlock called. Block [%ld] [%s]", p_access->info.i_pos, p_access->psz_location);
#endif

    uint64_t chunkNumberNeeded = _calculateChunkForPosition(p_access->info.i_pos, _lastSeenChunkSize);
    CCNxInterest *interest = _createInterestForChunk(p_access, p_access->psz_location, chunkNumberNeeded);

    // Once we know the chunk number, send an Interest for it. We'll get back the
    // corresponding ContentObject.

    if (ccnxPortal_Send(p_sys->portal, interest, CCNxStackTimeout_Never)) {

        msg_Info(p_access, "_CCNxBlock asked Portal for pos [%ld], chunk [%ld]", 
                            p_access->info.i_pos, chunkNumberNeeded);

        // Read the ContentObject response.
        CCNxMetaMessage *response = ccnxPortal_Receive(p_sys->portal, CCNxStackTimeout_Never);

        if (response != NULL) {
            if (ccnxMetaMessage_IsContentObject(response)) {
                //msg_Info(p_access, "_CCNxBlock received a chunk!");
                CCNxContentObject *contentObject = ccnxMetaMessage_GetContentObject(response);
               
                CCNxName *receivedName = ccnxContentObject_GetName(contentObject); 
                uint64_t receivedChunkNumber = ccnxVLCUtils_GetChunkNumberFromName(receivedName);

                // Extract the requested block from the ContentObject.
                size_t payloadSize = 0;
                p_block = _extractRequestedBlock(p_access, contentObject, 
                                                 _lastSeenChunkSize, receivedChunkNumber,
                                                 p_access->info.i_pos, &payloadSize);

                if (p_block) {
                    p_access->info.i_pos += p_block->i_size;
                }

                uint64_t finalChunkNum = ccnxContentObject_GetFinalChunkNumber(contentObject);

                if (receivedChunkNumber >= finalChunkNum) {
                    p_access->info.b_eof = true;
                    msg_Info(p_access, "EOF");
                } else {
                    p_access->info.b_eof = false;
                    _lastSeenChunkSize = payloadSize; // update the known chunk size
                } 

                ccnxContentObject_Release(&contentObject);

            } else {
                msg_Err(p_access, "_CCNxBlock received unexpected message from Portal.");
            }
        } else {
            msg_Err(p_access, "_CCNxBlock received unexpected null message from Portal.");
        }
    } else {
        msg_Err(p_access, "_CCNxBlock error writing to portal.");
    }
    
    ccnxInterest_Release(&interest);

    return (p_block);
}

/*****************************************************************************
 * _CCNxSeek:
 *****************************************************************************/
/* XXX - VLC behavior when playing an MP4 file is to seek back and forth for
 * the audio and video, which may be separated by many megabytes, so it is
 * a much better (and possibly required) that the code not discard all
 * previously buffered data when seeking, since the app is likely to seek back
 * close to where it was very quickly.
 */
static int 
_CCNxSeek(access_t *p_access, uint64_t i_pos)
{
    access_sys_t *p_sys = p_access->p_sys;
  
    p_access->info.i_pos = i_pos;

    // If we were in a streaming mode, we might send an interest with a particular 
    // chunk number here, so that that the flow controller starts fetching from that point.

    p_access->info.b_eof = false;
    msg_Info(p_access, "SEEK to i_pos [%ld]", i_pos);
    return (VLC_SUCCESS);
}

/*****************************************************************************
 * Control:
 *****************************************************************************/
static int
_CCNxControl(access_t *p_access, int i_query, va_list args)
{
    access_sys_t *p_sys = p_access->p_sys;
    bool   *pb_bool;
    int64_t      *pi_64;
    uint64_t      *pui_64;
    switch(i_query)
    {
        case ACCESS_CAN_SEEK:
        case ACCESS_CAN_FASTSEEK:
            pb_bool = (bool*)va_arg(args, bool *);
            *pb_bool = var_CreateGetBool(p_access, "ccn-streams-seekable");
            break;
            
        case ACCESS_CAN_PAUSE:
        case ACCESS_CAN_CONTROL_PACE:
            pb_bool = (bool*)va_arg(args, bool *);
            *pb_bool = true;
            break;

        // WALENDO: No longer in vlc_access.h?
        //case ACCESS_GET_SIZE:
        //    pui_64 = (uint64_t*)va_arg(args, uint64_t *);
        //    *pui_64 = p_sys->i_size;
        //    break;
            
        case ACCESS_GET_PTS_DELAY:
            pi_64 = (int64_t*)va_arg(args, int64_t *);
            *pi_64 = INT64_C(1000) *
            (int64_t) var_InheritInteger(p_access, "network-caching");
            break;
            
        case ACCESS_SET_PAUSE_STATE:
            pb_bool = (bool*)va_arg(args, bool *);
            break;
            
        case ACCESS_GET_TITLE_INFO:
        case ACCESS_GET_META:
        case ACCESS_SET_TITLE:
        case ACCESS_SET_SEEKPOINT:
        case ACCESS_SET_PRIVATE_ID_STATE:
    	case ACCESS_SET_PRIVATE_ID_CA:
        case ACCESS_GET_PRIVATE_ID_STATE:
        case ACCESS_GET_CONTENT_TYPE:
            return VLC_EGENERIC;
            
        default:
            msg_Warn(p_access, "_CCNxControl unimplemented query in control - %d", i_query);
            return VLC_EGENERIC;
            
    }
    return VLC_SUCCESS;
}

/*****************************************************************************
 * _CCNxOpen: 
 *****************************************************************************/
static int
_CCNxOpen(vlc_object_t *p_this)
{
    access_t     *p_access = (access_t *)p_this;
    access_sys_t *p_sys = NULL;
    int i_err = VLC_EGENERIC;

    msg_Info(p_access, "_CCNxOpen called [%s]", p_access->psz_location);

    p_sys = calloc(1, sizeof(access_sys_t));
    if (p_sys == NULL) {
        msg_Err(p_access, "_CCNxOpen failed. Could not allocated structure.");
        return(VLC_ENOMEM);
    }
     
    p_access->p_sys = p_sys;

    CCNxPortalFactory *portalFactory;
    if ((portalFactory = _setupPortalFactory()) != NULL) {
        p_sys->portal = 
            ccnxPortalFactory_CreatePortal(portalFactory,
                                           ccnxPortalRTA_Message    // message mode
                                           //ccnxPortalRTA_Chunked    // stream mode
                                           );

        ccnxPortalFactory_Release(&portalFactory);
    } else {
        msg_Err(p_access, "_CCNxOpen failed. Could not create PortalFactory.");
    }

    if (p_sys->portal == NULL) {
        msg_Err(p_access, "_CCNxOpen failed. Could not create Portal.");
        free(p_sys);
        return(VLC_EGENERIC);
    }

    msg_Info(p_access, "_CCNxOpen: portal open");

    // If we were using the Chunked mode, we could  start the chunks flowing
    // here. We can't do this until we support seeking in the flow controller, though.
    //CCNxInterest *interest = _createInterestForChunk(p_access, p_access->psz_location, 0);
    //ccnxPortal_Send(p_sys->portal, interest);
    //ccnxInterest_Release(&interest);

    /* Init p_access */
    access_InitFields(p_access);
    ACCESS_SET_CALLBACKS(NULL, _CCNxBlock, _CCNxControl, _CCNxSeek);
    return (VLC_SUCCESS);
}

/*****************************************************************************
 * _CCNxClose: free unused data structures
 *****************************************************************************/
static void
_CCNxClose(vlc_object_t *p_this)
{
    access_t     *p_access = (access_t *)p_this;
    access_sys_t *p_sys = p_access->p_sys;
    
    //msg_Info(p_access, "_CCNxClose called, missed %d blocks", p_sys->i_missed_co);
    msg_Info(p_access, "_CCNxClose called");

    if (p_sys != NULL) {
        ccnxPortal_Release(&p_sys->portal);
        if (p_sys->interestBaseName) {
            ccnxName_Release(&p_sys->interestBaseName);
        }
        free(p_sys);
    }

    msg_Info(p_access, "At exit, outstanding parcMemory allocations: %d", parcMemory_Outstanding());
}

