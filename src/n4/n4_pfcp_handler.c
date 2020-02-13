#define TRACE_MODULE _n4_pfcp_handler

#include <endian.h>
#include <arpa/inet.h>
#include <net/if.h>

#include "utlt_list.h"
#include "utlt_network.h"

#include "upf_context.h"
#include "pfcp_message.h"
#include "pfcp_xact.h"
#include "pfcp_convert.h"
#include "gtp_path.h"
#include "n4_pfcp_build.h"
#include "up/up_gtp_path.h"
#include "gtp5g.h"
#include "gtp5gnl.h"
#include "gtp_tunnel.h"


#define _PDR_ADD 0
#define _PDR_MOD 1
#define _PDR_DEL 2
#define _FAR_ADD 0
#define _FAR_MOD 1
#define _FAR_DEL 2


Status _pushPdrToKernel(struct gtp5g_pdr *pdr, int action) {
    UTLT_Assert(pdr, return STATUS_ERROR, "push PDR not found");
    Status status;

    uint16_t pdrId = *(uint16_t*)gtp5g_pdr_get_id(pdr);

    switch (action) {
    case _PDR_ADD:
        status = GtpTunnelAddPdr(gtp5g_int_name, pdr);
        UTLT_Assert(status == STATUS_OK, return STATUS_ERROR,
                    "Add PDR failed");
        break;
    case _PDR_MOD:
        status = GtpTunnelModPdr(gtp5g_int_name, pdr);
        UTLT_Assert(status == STATUS_OK, return STATUS_ERROR,
                    "Modify PDR failed");
        break;
    case _PDR_DEL:
        status = GtpTunnelDelPdr(gtp5g_int_name, pdrId);
        UTLT_Assert(status == STATUS_OK, return STATUS_ERROR,
                    "Delete PDR failed");
        break;
    default:
        UTLT_Assert(0, return STATUS_ERROR,
                    "PDR Action %d not defined", action);
    }

    return STATUS_OK;
}

Status _pushFarToKernel(struct gtp5g_far *far, int action) {
    UTLT_Assert(far, return STATUS_ERROR, "push FAR not found");
    Status status;

    uint32_t farId = *(uint32_t*)gtp5g_far_get_id(far);

    switch (action) {
    case _FAR_ADD:
        status = GtpTunnelAddFar(gtp5g_int_name, far);
        UTLT_Assert(status == STATUS_OK, return STATUS_ERROR,
                    "Add FAR failed");
        break;
    case _FAR_MOD:
        status = GtpTunnelModFar(gtp5g_int_name, far);
        UTLT_Assert(status == STATUS_OK, return STATUS_ERROR,
                    "Modify FAR failed");
        break;
    case _FAR_DEL:
        status = GtpTunnelDelFar(gtp5g_int_name, farId);
        UTLT_Assert(status == STATUS_OK, return STATUS_ERROR,
                    "Delete FAR failed");
        break;
    default:
        UTLT_Assert(0, return STATUS_ERROR,
                    "FAR Action %d not defined", action);
    }

    return STATUS_OK;
}

Status UpfN4HandleCreatePdr(UpfSession *session, CreatePDR *createPdr) {
    UpfPdr *tmpPdr = NULL;

    UTLT_Assert(createPdr->pDRID.presence, return STATUS_ERROR,
                "pdr id not presence");
    UTLT_Assert(createPdr->precedence.presence, return STATUS_ERROR,
                "precedence not presence");
    UTLT_Assert(createPdr->pDI.presence, return STATUS_ERROR,
                "Pdi not exist");
    UTLT_Assert(createPdr->pDI.sourceInterface.presence,
                return STATUS_ERROR, "PDI SourceInterface not presence");

    tmpPdr = gtp5g_pdr_alloc();
    UTLT_Assert(tmpPdr, return STATUS_ERROR, "pdr allocate error");

    // PdrId
    uint16_t pdrId = ntohs(*((uint16_t *)createPdr->pDRID.value));
    gtp5g_pdr_set_id(tmpPdr, pdrId);

    // precedence
    uint32_t precedence = ntohl(*((uint32_t *)createPdr->precedence.value));
    gtp5g_pdr_set_precedence(tmpPdr, precedence);

    // source interface
    //uint8_t sourceInterface = *((uint8_t *)(createPdr->pDI.sourceInterface.value));

    // F-TEID
    if (createPdr->pDI.localFTEID.presence) {
        PfcpFTeid *fTeid = (PfcpFTeid*)createPdr->pDI.localFTEID.value;
        uint32_t teid = ntohl(fTeid->teid);

        if (fTeid->v4 && fTeid->v6) {
            // TODO: Dual Stack
        } else if (fTeid->v4) {
            gtp5g_pdr_set_local_f_teid(tmpPdr, teid, &(fTeid->addr4));
        } else if (fTeid->v6) {
            // TODO: ipv6
            //gtp5g_pdr_set_local_f_teid(tmpPdr, teid, &(fTeid->addr6));
        }
    }

    // UE IP
    if (createPdr->pDI.uEIPAddress.presence) {
        PfcpUeIpAddr *ueIp =
          (PfcpUeIpAddr*)createPdr->pDI.uEIPAddress.value;
        if (ueIp->v4 && ueIp->v6) {
            // TODO: Dual Stack
        } else if (ueIp->v4) {
            gtp5g_pdr_set_ue_addr_ipv4(tmpPdr, &(ueIp->addr4));
        } else if (ueIp->v6) {
            // TODO: IPv6
        }
    }

    // Outer Header Removal
    if (createPdr->outerHeaderRemoval.presence) {
        uint8_t outerHeader =
          *(uint8_t*)createPdr->outerHeaderRemoval.value;
        gtp5g_pdr_set_outer_header_removal(tmpPdr, outerHeader);
    }

    // FAR ID
    if (createPdr->fARID.presence) {
        uint32_t farId = ntohl(*((uint8_t *)createPdr->fARID.value));
        gtp5g_pdr_set_far_id(tmpPdr, farId);
    }

    // Send PDR to kernel
    Status status = _pushPdrToKernel(tmpPdr, _PDR_ADD);
    UTLT_Assert(status == STATUS_OK, return STATUS_ERROR,
                "PDR not pushed to kernel");
    gtp5g_pdr_free(tmpPdr);
    UTLT_Assert(tmpPdr != NULL, return STATUS_ERROR,
                "Free PDR struct error");

    ListAppend(&session->pdrIdList, &pdrId);

    return STATUS_OK;
}

Status UpfN4HandleCreateFar(CreateFAR *createFar) {
    UpfFar *tmpFar = NULL;
    UTLT_Assert(createFar->fARID.presence, return STATUS_ERROR,
                "Far ID not presence");
    UTLT_Assert(createFar->applyAction.presence,
                return STATUS_ERROR, "Apply Action not presence");

    // Create FAR
    tmpFar = gtp5g_far_alloc();
    UTLT_Assert(tmpFar, return STATUS_ERROR, "FAR allocate error");

    // FarId
    uint32_t farId = ntohl(*((uint32_t *)createFar->fARID.value));
    gtp5g_far_set_id(tmpFar, farId);

    // Apply Action
    uint8_t applyAction = *((uint8_t *)(createFar->applyAction.value));
    gtp5g_far_set_apply_action(tmpFar, applyAction);

    // Forwarding Parameters
    if (createFar->forwardingParameters.presence) {
        // Destination Interface
        /*
        if (createFar->forwardingParameters.destinationInterface.presence) {
            uint8_t destinationInterface =
              *((uint8_t *)(createFar->forwardingParameters.destinationInterface.value));
        }
        // Network Instance
        if (createFar->forwardingParameters.networkInstance.presence) {
        }
        */
        // Outer Header Creation
        if (createFar->forwardingParameters.outerHeaderCreation.presence) {
            PfcpOuterHdr *outerHdr =
              (PfcpOuterHdr *)(createFar->forwardingParameters.outerHeaderCreation.value);
            uint16_t description = *((uint16_t *)outerHdr);

            if (outerHdr->gtpuIpv4 || outerHdr->udpIpv4) {
                gtp5g_far_set_outer_header_creation(tmpFar, description,
                  ntohl(outerHdr->teid), &(outerHdr->addr4), 2152);
            } else if (outerHdr->udpIpv4) {
                // only with UDP enabled has port number
                gtp5g_far_set_outer_header_creation(tmpFar, description,
                  0, &(outerHdr->addr4), ntohl(outerHdr->port));
            }
        }
    }

    // Send FAR to kernel
    Status status = _pushFarToKernel(tmpFar, _FAR_ADD);
    UTLT_Assert(status == STATUS_OK, return STATUS_ERROR,
                "FAR not pushed to kernel");
    gtp5g_far_free(tmpFar);
    UTLT_Assert(tmpFar != NULL, return STATUS_ERROR,
                "Free FAR struct error");

    return STATUS_OK;
}

Status UpfN4HandleUpdatePdr(UpdatePDR *updatePdr) {
    UpfPdr *tmpPdr = NULL;
    UTLT_Assert(updatePdr->pDRID.presence == 1,
                return STATUS_ERROR, "updatePDR no pdrId");

    // Find PDR
    uint16_t pdrId = ntohs(*((uint16_t*)updatePdr->pDRID.value));
    tmpPdr = GtpTunnelFindPdrById(gtp5g_int_name, pdrId);
    UTLT_Assert(tmpPdr, return STATUS_ERROR,
                "[PFCP] UpdatePDR PDR[%u] not found", pdrId);

    // TODO: other IE of update PDR
    if (updatePdr->outerHeaderRemoval.presence) {
        gtp5g_pdr_set_outer_header_removal(tmpPdr,
          *((uint8_t*)(updatePdr->outerHeaderRemoval.value)));
    }
    if (updatePdr->precedence.presence) {
        gtp5g_pdr_set_precedence(tmpPdr,
          *((uint32_t *)(updatePdr->precedence.value)));
    }
    if (updatePdr->pDI.presence) {
        if (updatePdr->pDI.localFTEID.presence) {
            PfcpFTeid *fTeid = (PfcpFTeid*)updatePdr->pDI.localFTEID.value;
            uint32_t teid = ntohl(fTeid->teid);

            if (fTeid->v4 && fTeid->v6) {
                // TODO: Dual Stack
            } else if (fTeid->v4) {
                gtp5g_pdr_set_local_f_teid(tmpPdr, teid, &(fTeid->addr4));
            } else if (fTeid->v6) {
                // TODO: ipv6
                //gtp5g_pdr_set_local_f_teid(tmpPdr, teid, &(fTeid->addr6));
            }
        }
        if (updatePdr->pDI.uEIPAddress.presence) {
            PfcpUeIpAddr *ueIp =
              (PfcpUeIpAddr*)updatePdr->pDI.uEIPAddress.value;
            if (ueIp->v4 && ueIp->v6) {
                // TODO: Dual Stack
            } else if (ueIp->v4) {
                gtp5g_pdr_set_ue_addr_ipv4(tmpPdr, &(ueIp->addr4));
            } else if (ueIp->v6) {
                // TODO: IPv6
            }
        }
    }
    if (updatePdr->fARID.presence) {
      gtp5g_pdr_set_far_id(tmpPdr, *(uint32_t *)updatePdr->fARID.value);
    }

    // update PDR to kernel
    Status status = _pushPdrToKernel(tmpPdr, _PDR_MOD);
    UTLT_Assert(status == STATUS_OK, return STATUS_ERROR,
                "PDR not pushed to kernel");
    gtp5g_pdr_free(tmpPdr);
    UTLT_Assert(tmpPdr != NULL, return STATUS_ERROR,
                "Free PDR struct error");

    return STATUS_OK;
}

Status UpfN4HandleUpdateFar(UpdateFAR *updateFar) {
    UpfFar *tmpFar = NULL;
    UTLT_Assert(updateFar->fARID.presence,
                return STATUS_ERROR, "Far ID not presence");

    // Find FAR
    uint32_t farId = ntohl(*((uint32_t *)updateFar->fARID.value));
    tmpFar = GtpTunnelFindFarById(gtp5g_int_name, farId);
    UTLT_Assert(tmpFar, return STATUS_ERROR,
                "[PFCP] UpdateFAR FAR[%u] not found", farId);

    // update ApplyAction
    if (updateFar->applyAction.presence) {
        gtp5g_far_set_apply_action(tmpFar,
          *(uint8_t *)updateFar->applyAction.value);
    }
    // update Forwarding parameters
    if (updateFar->updateForwardingParameters.outerHeaderCreation.value) {
        PfcpOuterHdr *outerHdr = (PfcpOuterHdr *)
          (updateFar->updateForwardingParameters.outerHeaderCreation.value);
        uint16_t description = *((uint16_t *)outerHdr);

        if (outerHdr->gtpuIpv4) {
            gtp5g_far_set_outer_header_creation(tmpFar, description,
              ntohl(outerHdr->teid), &(outerHdr->addr4), 2152);
        } else if (outerHdr->udpIpv4) {
            gtp5g_far_set_outer_header_creation(tmpFar, description,
              0, &(outerHdr->addr4), ntohl(outerHdr->port));
        }
    }
    // TODO: update Duplicating parameters
    // TODO: update BAR

    // TODO: update FAR to kernel
    Status status = _pushFarToKernel(tmpFar, _FAR_MOD);
    UTLT_Assert(status == STATUS_OK, return STATUS_ERROR,
                "FAR not pushed to kernel");
    gtp5g_far_free(tmpFar);
    UTLT_Assert(tmpFar != NULL, return STATUS_ERROR,
                "Free FAR struct error");

    return STATUS_OK;
}

Status UpfN4HandleRemovePdr(UpfSession *session, uint16_t pdrId) {
    UTLT_Assert(pdrId, return STATUS_ERROR, "pdrId cannot be 0");
    UTLT_Assert(session, return STATUS_ERROR,
                "session not found");

    uint16_t *idPtr = ListFirst(&session->pdrIdList);
    while (idPtr) {
        if (*idPtr == pdrId) {
            Status status = GtpTunnelDelPdr(gtp5g_int_name, pdrId);
            UTLT_Assert(status == STATUS_OK, return STATUS_ERROR,
                        "PDR[%u] delete failed", pdrId);
            ListRemove(&session->pdrIdList, idPtr);
            return STATUS_OK;
        }

        idPtr = ListNext(idPtr);
    }

    UTLT_Warning("PDR[%u] not in this session, PDR not removed", pdrId);
    return STATUS_ERROR;
}

Status UpfN4HandleRemoveFar(uint32_t farId) {
    UTLT_Assert(farId, return STATUS_ERROR,
                "farId should not be 0");

    // TODO: here can be speedup like
    //UpfPdr *pdr = GtpTunnelFindPdrByFarId(gtp5g_int_name, farId);
    //if (pdr) {
    //    gtp5g_pdr_set_far_id(pdr, 0);
    //}
    //Status status = GtpTunnelDelFar(gtp5g_int_name, farId);
    //UTLT_Assert(status == STATUS_OK, return STATUS_ERROR,
    //            "FAR delete error");

    UpfFar *far = GtpTunnelFindFarById(gtp5g_int_name, farId);
    UTLT_Assert(far != NULL, return STATUS_ERROR,
                "Cannot find FAR[%u] by FarId", farId);

    // Set FarId to 0 if the PDR has this far
    int pdrNum = *(int*)gtp5g_far_get_related_pdr_num(far);
    uint16_t *pdrList = gtp5g_far_get_related_pdr_list(far);
    for (size_t idx = 0; idx < pdrNum; ++idx) {
        gtp5g_pdr_set_far_id(pdrList[idx], 0);
    }

    Status status = _pushFarToKernel(far, _FAR_DEL);
    UTLT_Assert(status == STATUS_OK, return STATUS_ERROR,
                "FAR not pushed to kernel");
    gtp5g_far_free(far);
    UTLT_Assert(far != NULL, return STATUS_ERROR,
                "Remove FAR error");

    return STATUS_OK;
}

Status UpfN4HandleSessionEstablishmentRequest(
        UpfSession *session, PfcpXact *pfcpXact,
        PFCPSessionEstablishmentRequest *request) {
    Status status;

    UTLT_Assert(session, return STATUS_ERROR, "Upf Session error");
    UTLT_Assert(pfcpXact, return STATUS_ERROR, "pfcpXact error");
    //UTLT_Assert(pfcpXact->gtpBuf, return,
    //  "GTP buffer of pfcpXact error");
    //UTLT_Assert(pfcpXact->gtpXact, return,
    // "GTP Xact of pfcpXact error");

    if (request->createPDR[0].presence) {
        status = UpfN4HandleCreatePdr(session, &request->createPDR[0]);
        UTLT_Assert(status == STATUS_OK, return STATUS_ERROR,
                    "Create PDR Error");
    }
    if (request->createPDR[1].presence) {
        status = UpfN4HandleCreatePdr(session, &request->createPDR[1]);
        UTLT_Assert(status == STATUS_OK, return STATUS_ERROR,
                    "Create PDR 2 Error");
    }
    if (request->createFAR[0].presence) {
        status = UpfN4HandleCreateFar(&request->createFAR[0]);
        UTLT_Assert(status == STATUS_OK, return STATUS_ERROR,
                    "Create FAR error");
    }
    if (request->createPDR[1].presence) {
        status = UpfN4HandleCreateFar(&request->createFAR[1]);
        UTLT_Assert(status == STATUS_OK, return STATUS_ERROR,
                    "Create FAR error");
    }
    if (request->createURR.presence) {
        // TODO
    }
    if (request->createBAR.presence) {
        // TODO
    }
    if (request->createQER.presence) {
        // TODO
    }

    PfcpHeader header;
    Bufblk *bufBlk = NULL;
    PfcpFSeid *smfFSeid = NULL;

    if (!request->cPFSEID.presence) {
        UTLT_Error("Session Establishment Response: No CP F-SEID");
        return STATUS_ERROR;
    }

    smfFSeid = request->cPFSEID.value;
    session->smfSeid = be64toh(smfFSeid->seid);

    /* Send Response */
    memset(&header, 0, sizeof(PfcpHeader));
    header.type = PFCP_SESSION_ESTABLISHMENT_RESPONSE;
    header.seid = session->smfSeid;

    status = UpfN4BuildSessionEstablishmentResponse(
        &bufBlk, header.type, session, request);
    UTLT_Assert(status == STATUS_OK, return STATUS_ERROR,
                "N4 build error");

    status = PfcpXactUpdateTx(pfcpXact, &header, bufBlk);
    UTLT_Assert(status == STATUS_OK, return STATUS_ERROR,
                "pfcpXact update TX error");

    status = PfcpXactCommit(pfcpXact);
    UTLT_Assert(status == STATUS_OK, return STATUS_ERROR,
                "xact commit error");

    UTLT_Info("[PFCP] Session Establishment Response");
    return STATUS_OK;
}

Status UpfN4HandleSessionModificationRequest(
        UpfSession *session, PfcpXact *xact,
        PFCPSessionModificationRequest *request) {
    UTLT_Assert(session, return STATUS_ERROR, "Session error");
    UTLT_Assert(xact, return STATUS_ERROR, "xact error");

    Status status;
    PfcpHeader header;
    Bufblk *bufBlk;

    /* Create PDR */
    if (request->createPDR[0].presence) {
        status = UpfN4HandleCreatePdr(session, &request->createPDR[0]);
        UTLT_Assert(status == STATUS_OK, return STATUS_ERROR,
                    "Modification: Create PDR error");
    }
    if (request->createPDR[1].presence) {
        status = UpfN4HandleCreatePdr(session, &request->createPDR[1]);
        UTLT_Assert(status == STATUS_OK, return STATUS_ERROR,
                    "Modification: Create PDR2 error");
    }
    /* Create FAR */
    if (request->createFAR[0].presence) {
        status = UpfN4HandleCreateFar(&request->createFAR[0]);
        UTLT_Assert(status == STATUS_OK, return STATUS_ERROR,
                    "Modification: Create FAR error");
    }
    if (request->createFAR[1].presence) {
        status = UpfN4HandleCreateFar(&request->createFAR[1]);
        UTLT_Assert(status == STATUS_OK, return STATUS_ERROR,
                    "Modification: Create FAR2 error");
    }

    /* Update PDR */
    if (request->updatePDR.presence) {
        status = UpfN4HandleUpdatePdr(&request->updatePDR);
        UTLT_Assert(status == STATUS_OK, return STATUS_ERROR,
                    "Modification: Update PDR error");
    }
    /* Update FAR */
    if (request->updateFAR.presence) {
        status = UpfN4HandleUpdateFar(&request->updateFAR);
        UTLT_Assert(status == STATUS_OK, return STATUS_ERROR,
                    "Modification: Update FAR error");
    }
    /* Remove PDR */
    if (request->removePDR.presence) {
        UTLT_Assert(request->removePDR.pDRID.presence == 1, ,
                    "[PFCP] PdrId in removePDR not presence!");
        status = UpfN4HandleRemovePdr(session,
                   *(uint16_t*)request->removePDR.pDRID.value);
        UTLT_Assert(status == STATUS_OK, return STATUS_ERROR,
                    "Modification: Remove PDR error");
    }
    /* Remove FAR */
    if (request->removeFAR.presence) {
        UTLT_Assert(request->removeFAR.fARID.presence == 1, ,
                    "[PFCP] FarId in removeFAR not presence");
        status = UpfN4HandleRemoveFar(
                   *(uint32_t*)request->removeFAR.fARID.value);
        UTLT_Assert(status == STATUS_OK, return STATUS_ERROR,
                    "Modification: Remove FAR error");
    }

    /* Send Session Modification Response */
    memset(&header, 0, sizeof(PfcpHeader));
    header.type = PFCP_SESSION_MODIFICATION_RESPONSE;
    header.seid = session->smfSeid;

    status = UpfN4BuildSessionModificationResponse(
        &bufBlk, header.type, session, request);
    UTLT_Assert(status == STATUS_OK, return STATUS_ERROR,
                "N4 build error");

    status = PfcpXactUpdateTx(xact, &header, bufBlk);
    UTLT_Assert(status == STATUS_OK, return STATUS_ERROR,
                "PfcpXactUpdateTx error");

    status = PfcpXactCommit(xact);
    UTLT_Assert(status == STATUS_OK, return STATUS_ERROR,
                "PFCP Commit error");

    UTLT_Info("[PFCP] Session Modification Response");
    return STATUS_OK;
}

Status UpfN4HandleSessionDeletionRequest(UpfSession *session,
       PfcpXact *xact, PFCPSessionDeletionRequest *request) {
    UTLT_Assert(session, return STATUS_ERROR, "session error");
    UTLT_Assert(xact, return STATUS_ERROR, "xact error");

    Status status;
    PfcpHeader header;
    Bufblk *bufBlk = NULL;

    // TODO: Remove all PDR
    // TODO: Remove all FAR
    for (uint16_t *pdrId = ListFirst(&session->pdrIdList);
         pdrId != NULL; pdrId = ListNext(pdrId)) {
        status = GtpTunnelDelPdr(gtp5g_int_name, pdrId);
        UTLT_Assert(status == STATUS_OK, return STATUS_ERROR,
                    "Remove PDR[%u] error", pdrId);
    }

    /* delete session */
    UpfSessionRemove(session);

    /* Send Session Deletion Response */
    memset(&header, 0, sizeof(PfcpHeader));

    header.type = PFCP_SESSION_DELETION_RESPONSE;
    header.seid = session->smfSeid;

    status = UpfN4BuildSessionDeletionResponse(&bufBlk,
               header.type, session, request);
    UTLT_Assert(status == STATUS_OK, return STATUS_ERROR,
                "N4 build error");

    status = PfcpXactUpdateTx(xact, &header, bufBlk);
    UTLT_Assert(status == STATUS_OK, return STATUS_ERROR,
                "PfcpXactUpdateTx error");

    status = PfcpXactCommit(xact);
    UTLT_Assert(status == STATUS_OK, return STATUS_ERROR,
                "xact commit error");

    UTLT_Info("[PFCP] Session Deletion Response");
    return STATUS_OK;
}

Status UpfN4HandleSessionReportResponse(
         UpfSession *session, PfcpXact *xact,
         PFCPSessionReportResponse *response) {
    Status status;

    UTLT_Assert(session, return STATUS_ERROR, "Session error");
    UTLT_Assert(xact, return STATUS_ERROR, "xact error");
    UTLT_Assert(response->cause.presence, return STATUS_ERROR,
                "SessionReportResponse error: no Cause");

    // TODO: check if need update TX

    status = PfcpXactCommit(xact);
    UTLT_Assert(status == STATUS_OK, return STATUS_ERROR,
                "xact commit error");

    UTLT_Info("[PFCP] Session Report Response");
    return STATUS_OK;
}

Status UpfN4HandleAssociationSetupRequest(PfcpXact *xact,
       PFCPAssociationSetupRequest *request) {
    PfcpNodeId *nodeId;

    UTLT_Assert(xact, return STATUS_ERROR, "xact error");
    UTLT_Assert(xact->gnode, return STATUS_ERROR,
                "gNode of xact error");
    UTLT_Assert(request->nodeID.presence, return STATUS_ERROR,
                "Request missing nodeId");

    nodeId = (PfcpNodeId *)request->nodeID.value;

    xact->gnode->nodeId.type = nodeId->type;
    switch (nodeId->type) {
        case PFCP_NODE_ID_IPV4:
            xact->gnode->nodeId.addr4 = nodeId->addr4;
            break;
        case PFCP_NODE_ID_IPV6:
            xact->gnode->nodeId.addr6 = nodeId->addr6;
            break;
        default:
            UTLT_Assert(0, return STATUS_ERROR,
                        "Request no node id type");
            break;
    }

    /* Accept */
    xact->gnode->state = PFCP_NODE_ST_ASSOCIATED;

    Status status;
    PfcpHeader header;
    Bufblk *bufBlk = NULL;

    /* Send */
    memset(&header, 0, sizeof(PfcpHeader));
    header.type = PFCP_ASSOCIATION_SETUP_RESPONSE;
    header.seid = 0;

    status = UpfN4BuildAssociationSetupResponse(&bufBlk, header.type);
    UTLT_Assert(status == STATUS_OK, return STATUS_ERROR,
                "N4 build error");

    status = PfcpXactUpdateTx(xact, &header, bufBlk);
    UTLT_Assert(status == STATUS_OK, return STATUS_ERROR,
                "PfcpXactUpdateTx error");

    status = PfcpXactCommit(xact);
    UTLT_Assert(status == STATUS_OK, return STATUS_ERROR,
                "xact commit error");

    UTLT_Info("[PFCP] Association Session Setup Response");
    return STATUS_OK;
}

Status UpfN4HandleAssociationUpdateRequest(
         PfcpXact *xact, PFCPAssociationUpdateRequest *request) {
    // TODO
    UTLT_Info("[PFCP] TODO Association Update Request");
    return STATUS_OK;
}

Status UpfN4HandleAssociationReleaseRequest(
         PfcpXact *xact, PFCPAssociationReleaseRequest *request) {
    // TODO
    UTLT_Info("[PFCP] TODO Association Release Request");
    return STATUS_OK;
}

Status UpfN4HandleHeartbeatRequest(
         PfcpXact *xact, HeartbeatRequest *request) {
    Status status;
    PfcpHeader header;
    Bufblk *bufBlk = NULL;

    UTLT_Info("[PFCP] Heartbeat Request");

    /* Send */
    memset(&header, 0, sizeof(PfcpHeader));
    header.type = PFCP_HEARTBEAT_RESPONSE;
    header.seid = 0;

    status = UpfN4BuildHeartbeatResponse(&bufBlk, header.type);
    UTLT_Assert(status == STATUS_OK, return STATUS_ERROR,
                "N4 build error");

    status = PfcpXactUpdateTx(xact, &header, bufBlk);
    UTLT_Assert(status == STATUS_OK, return STATUS_ERROR,
                "PfcpXactUpdateTx error");

    status = PfcpXactCommit(xact);
    UTLT_Assert(status == STATUS_OK, return STATUS_ERROR,
                "xact commit error");

    UTLT_Info("[PFCP] Heartbeat Response");
    return STATUS_OK;
}

Status UpfN4HandleHeartbeatResponse(
         PfcpXact *xact, HeartbeatResponse *response) {
    // if rsv response, nothing to do, else peer may be not alive
    UTLT_Info("[PFCP] Heartbeat Response");
    return STATUS_OK;
}
