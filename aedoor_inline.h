/*
 * Local AEDoor inline bindings copied from the proven door work.
 *
 * This stays close to the original library interface so the higher-level
 * bridge can keep the rest of arbfiles tidy.
 */
#ifndef AEDOOR_INLINE_H
#define AEDOOR_INLINE_H

#include <exec/libraries.h>
#include <exec/types.h>

/* Library name, door constants, and data IDs used by arbfiles. */
#define AEDoorName "AEDoor.library"

struct DIFace {
  APTR dif_AEPort;
  APTR dif_MsgPort;
  APTR dif_Message;
  char dif_ReplyName[16];
  int *dif_Data;
  char *dif_String;
};

#define NOLF 0
#define LF 1
#define WSF_LF 1

#define DT_NAME 100
#define BB_CONFNAME 126
#define BB_CONFLOCAL 127
#define BB_STATUS 129
#define GETKEY 500
#define RAWARROW 501
#define NODE_DEVICE 503
#define NODE_UNIT 504
#define BB_CONFNUM 510
#define NODE_BAUDRATE 516
#define BB_PURGELINE 522
#define BB_PURGELINESTART 523
#define BB_PURGELINEEND 524
#define BB_NONSTOPTEXT 525
#define BB_LINECOUNT 526
#define DT_ISANSI 541
#define CON_CURSOR 705

/* Inline library calls used by the bridge layer. */
extern struct Library *AEDBase;

struct DIFace * __CreateComm(__reg("a6") void *, __reg("d0") unsigned long node)="\tjsr\t-30(a6)";
#define CreateComm(node) __CreateComm(AEDBase, (node))

void __DeleteComm(__reg("a6") void *, __reg("a1") struct DIFace *dif)="\tjsr\t-36(a6)";
#define DeleteComm(dif) __DeleteComm(AEDBase, (dif))

void __SendCmd(__reg("a6") void *, __reg("a1") struct DIFace *dif, __reg("d0") unsigned long command)="\tjsr\t-42(a6)";
#define SendCmd(dif, command) __SendCmd(AEDBase, (dif), (command))

void __SendDataCmd(__reg("a6") void *, __reg("a1") struct DIFace *dif, __reg("d0") unsigned long command, __reg("d1") unsigned long data)="\tjsr\t-54(a6)";
#define SendDataCmd(dif, command, data) __SendDataCmd(AEDBase, (dif), (command), (data))

char * __GetString(__reg("a6") void *, __reg("a1") struct DIFace *dif)="\tjsr\t-72(a6)";
#define GetString(dif) __GetString(AEDBase, (dif))

void __WriteStr(__reg("a6") void *, __reg("a1") struct DIFace *dif, __reg("a0") char *text, __reg("d1") unsigned long flags)="\tjsr\t-84(a6)";
#define WriteStr(dif, text, flags) __WriteStr(AEDBase, (dif), (text), (flags))

void __GetDT(__reg("a6") void *, __reg("a1") struct DIFace *dif, __reg("d0") unsigned long id, __reg("a0") char *text)="\tjsr\t-108(a6)";
#define GetDT(dif, id, text) __GetDT(AEDBase, (dif), (id), (text))

void __SetDT(__reg("a6") void *, __reg("a1") struct DIFace *dif, __reg("d0") unsigned long id, __reg("a0") char *text)="\tjsr\t-102(a6)";
#define SetDT(dif, id, text) __SetDT(AEDBase, (dif), (id), (text))

long __Hotkey(__reg("a6") void *, __reg("a1") struct DIFace *dif, __reg("a0") char *prompt)="\tjsr\t-126(a6)";
#define Hotkey(dif, prompt) __Hotkey(AEDBase, (dif), (prompt))

#endif
