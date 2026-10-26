/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "mul_plan_radio.h"
#if MUL_UNIVERSAL
#include "qorvo_platform.h"
#include "dw3000_deca_regs.h"
#include <string.h>
#define DTU_MS UINT64_C(63897600)
#define RX_EVENTS (SYS_STATUS_ALL_RX_GOOD|SYS_STATUS_ALL_RX_TO|SYS_STATUS_ALL_RX_ERR|DWT_INT_RXOVRR_BIT_MASK)
static struct {
 const mulRangingPlan_t *plan;const mulClock_t *clock;
 uint64_t stamps[6],expected,deadline,end,referenceEpoch;
 mulPower_t power[3];
 uint32_t cycle,retired,completed,rejected,aborted,measuredMs;
 uint16_t rawTof,tof;
 uint8_t local,operation,stage;
 mulPlanRangeResult_t result;
 bool reference,active,pending,resultReady,retry;
} ar;
static uint64_t nowStamp(void) {
 uint8_t t[4];dwt_readsystime(t);uint64_t n=255;
 for(unsigned i=0;i<4;i++)n|=(uint64_t)t[i]<<(8*(i+1));
 return n; // Upper edge of the hardware256-DTU bin; never uninitialized byte5.
}
static uint64_t usDtu(uint32_t us){return ((uint64_t)us*DTU_MS+999)/1000;}
static void rearm(void){dwt_forcetrxoff();dwt_writesysstatuslo(RX_EVENTS|DWT_INT_TXFRS_BIT_MASK|SYS_STATUS_HPDWARN_BIT_MASK);ar.retry=dwt_rxenable(DWT_START_RX_IMMEDIATE)!=DWT_SUCCESS;}
static void cancel(void){if(ar.active)ar.aborted++;ar.active=ar.pending=false;rearm();}
void mul_plan_radio_suspend(void){ar.active=ar.pending=ar.reference=ar.retry=ar.resultReady=false;}
bool mul_plan_radio_busy(void){return ar.active;}
void mul_plan_radio_configure(const mulRangingPlan_t *plan,uint8_t local,const mulClock_t *clock) {
 memset(&ar,0,sizeof(ar));ar.local=local;ar.clock=clock;
 if(plan&&!mulRangingPlanValidate(plan,NULL)&&(plan->enabled&MUL_PLAN_AR))ar.plan=plan;
}
void mul_plan_radio_reference(uint32_t cycle,uint16_t captureAgeMs) {
 if(ar.reference&&ar.cycle==cycle&&ar.clock&&ar.referenceEpoch==ar.clock->epoch)return;
 ar.active=ar.pending=false;ar.reference=ar.plan&&captureAgeMs<=15;
 ar.referenceEpoch=ar.clock?ar.clock->epoch:0;ar.cycle=cycle;ar.retired=0;
}
bool mul_plan_radio_take_result(mulPlanRangeResult_t *out) {
 if(!out||!ar.resultReady)return false;
 *out=ar.result;ar.resultReady=false;return true;
}
static void commitResult(void) {
 mulRangingOperation_t op;(void)mulRangingPlanOperation(ar.plan,ar.cycle,ar.operation,&op);
 ar.result=(mulPlanRangeResult_t){.cycle=ar.cycle,.measuredMs=ar.measuredMs,.fingerprint=mulRangingPlanFingerprint(ar.plan),
  .rawTof=ar.rawTof,.tof=ar.tof,.operation=ar.operation,.initiator=op.initiator,.responder=op.responder};
 memcpy(ar.result.stamps,ar.stamps,sizeof(ar.stamps));memcpy(ar.result.power,ar.power,sizeof(ar.power));ar.resultReady=true;
}
static mulPower_t unsupported(void){return (mulPower_t){.powerCentiDbm=INT16_MIN,.status=mulPowerUnsupported};}
static bool powerEqual(const mulPower_t *a,const mulPower_t *b){return a->powerCentiDbm==b->powerCentiDbm&&a->deltaDtu==b->deltaDtu&&a->status==b->status&&a->table==b->table;}
static bool sendReply(mulTwrType_t type,uint64_t received) {
 const mulSchedule_t *s=&ar.plan->broadcast;
 if(dwt_gettxantennadelay()!=uwb_runtime_active()->tx_ant_dtu||dwt_getrxantennadelay()!=uwb_runtime_active()->rx_ant_dtu){cancel();return false;}
 uint64_t target=mulTimestampMask(received+ar.plan->replyMs*DTU_MS);
 uint64_t raw=mulTimestampMask((mulTimestampMask(target-dwt_gettxantennadelay())+511)&~UINT64_C(511));
 uint64_t lead=usDtu(s->txLeadUs),tail=usDtu(s->airtimeUs+s->rxBudgetUs);
 if(mulTimestampDifference(raw,nowStamp())<(int64_t)lead||mulTimestampDifference(ar.end,target)<(int64_t)tail){cancel();return false;}
 dwt_forcetrxoff();dwt_writesysstatuslo(RX_EVENTS|DWT_INT_TXFRS_BIT_MASK|SYS_STATUS_HPDWARN_BIT_MASK);
 dwt_setdelayedtrxtime((uint32_t)(raw>>8));
 uint32_t verified=dwt_read_reg(DX_TIME_ID)&DX_TIME_DX_TIME_BIT_MASK;
 if(verified!=(uint32_t)(raw>>8)){cancel();return false;}
 ar.expected=mulTimestampMask(((uint64_t)verified<<8)+dwt_gettxantennadelay());
 if(type==mulTwrResponse)ar.stamps[2]=ar.expected;
 mulTwrPacket_t p={.type=type,.id=ar.cycle,.txTimestamp=ar.expected};
 memcpy(p.stamps,ar.stamps,sizeof(p.stamps));memcpy(p.power,ar.power,sizeof(p.power));
 uint8_t frame[MUL_MULTI_TWR_BYTES];
 if(!mulPlanTwrEncode(frame,ar.plan,ar.operation,&p)||dwt_writetxdata(sizeof(frame),frame,0)!=DWT_SUCCESS){cancel();return false;}
 dwt_writetxfctrl(sizeof(frame)+2,0,1);dwt_setrxaftertxdelay(0);
 if(mulTimestampDifference(raw,nowStamp())<(int64_t)lead){cancel();return false;}
 ar.stage=(uint8_t)type;ar.pending=true;
 ar.deadline=mulTimestampMask(ar.expected+ar.plan->replyMs*DTU_MS+usDtu(s->airtimeUs+s->rxBudgetUs+100u*s->guard100us));
 if(dwt_starttx(DWT_START_TX_DELAYED|DWT_RESPONSE_EXPECTED)!=DWT_SUCCESS){cancel();return false;}
 return true;
}
static bool window(unsigned index,uint32_t now,uint64_t *start,uint64_t *end) {
 mulRangingOperation_t op;
 if(!ar.reference||!ar.clock||ar.clock->sequence!=ar.cycle||!mulRangingPlanOperation(ar.plan,ar.cycle,index,&op)||
   op.kind!=MUL_PLAN_AR||op.responder!=ar.local)return false;
 mulClockMapping_t mapped;
 if(!mulClockMapRefToFollower(ar.clock,mulTimestampMask(ar.clock->referenceTx+usDtu(op.startUs)),now,&mapped))return false;
 *start=mapped.timestamp;
 if(!mulClockMapRefToFollower(ar.clock,mulTimestampMask(ar.clock->referenceTx+usDtu(op.endUs)),now,&mapped))return false;
 *end=mapped.timestamp;return true;
}
static void receive(const uint8_t *frame,uint64_t rx,uint32_t now) {
 unsigned index=frame[85];mulTwrPacket_t p;uint64_t start,end;
 if(index>=MUL_PLAN_MAX_OPERATIONS||!window(index,now,&start,&end)||
   !mulPlanTwrDecode(ar.plan,ar.cycle,index,frame,MUL_MULTI_TWR_BYTES,&p)||
   mulTimestampDifference(rx,start)<0||mulTimestampDifference(rx,end)>=0||
   mulTimestampDifference(nowStamp(),rx)<0||mulTimestampDifference(nowStamp(),rx)>15*(int64_t)DTU_MS) {
  ar.rejected++;if(ar.active)cancel();else rearm();return;
 }
 if(p.type==mulTwrPoll&&!ar.active) {
  uint32_t bit=UINT32_C(1)<<index;if(ar.retired&bit){ar.rejected++;rearm();return;}
  ar.retired|=bit;ar.operation=(uint8_t)index;ar.end=end;ar.active=true;ar.resultReady=false;
  memset(ar.stamps,0,sizeof(ar.stamps));memset(ar.power,0,sizeof(ar.power));
  ar.stamps[0]=p.stamps[0];ar.stamps[1]=rx;ar.power[0]=unsupported();sendReply(mulTwrResponse,rx);return;
 }
 unsigned prefix=p.type==mulTwrFinal?3:6;
 bool match=ar.active&&!ar.pending&&index==ar.operation&&p.type==ar.stage&&!memcmp(p.stamps,ar.stamps,prefix*sizeof(uint64_t));
 for(unsigned i=0;i<prefix/2;i++)if(!powerEqual(&p.power[i],&ar.power[i]))match=false;
 if(!match){ar.rejected++;if(ar.active)cancel();else rearm();return;}
 if(p.type==mulTwrFinal) {
  ar.stamps[3]=p.stamps[3];ar.stamps[4]=p.stamps[4];ar.stamps[5]=rx;ar.power[1]=p.power[1];ar.power[2]=unsupported();
  uint32_t raw,corrected;
  if(!mulTwrResultsReply(ar.stamps,ar.power,&raw,&corrected,ar.plan->replyMs)){cancel();return;}
  ar.rawTof=(uint16_t)raw;ar.tof=(uint16_t)(corrected==UINT32_MAX?raw:corrected);sendReply(mulTwrReport,rx);
 }else if(p.type==mulTwrResult) {
  uint32_t raw,corrected;int64_t span=mulTimestampDifference(p.txTimestamp,ar.stamps[4]);
  if(span<3*(int64_t)ar.plan->replyMs*(int64_t)DTU_MS/2||span>3*(int64_t)ar.plan->replyMs*(int64_t)DTU_MS||
    !mulTwrResultsReply(ar.stamps,ar.power,&raw,&corrected,ar.plan->replyMs)||p.tof!=(corrected==UINT32_MAX?raw:corrected)) {cancel();return;}
  int64_t elapsed=mulTimestampDifference(nowStamp(),ar.stamps[1]);
  if(elapsed<0||elapsed>200*(int64_t)DTU_MS){cancel();return;}
  ar.measuredMs=now-(uint32_t)(((uint64_t)elapsed+DTU_MS-1)/DTU_MS);ar.completed++;commitResult();
  ar.active=ar.pending=false;rearm();
 }else {ar.rejected++;cancel();}
}
bool mul_plan_radio_process(uint32_t now,uint32_t status) {
 if(!ar.plan)return false;
 if(status&DWT_INT_RXFCG_BIT_MASK) {
  unsigned n=dwt_read_reg(RX_FINFO_ID)&RX_FINFO_RXFLEN_BIT_MASK;
  if(n==MUL_MULTI_REFERENCE_BYTES+2||n==MUL_MULTI_FOLLOWER_BYTES+2) {
   // Mandatory broadcast priority also applies when a ranging deadline passed.
   // Do not clear this frozen RX before hybridProcess captures its bytes/stamp.
   if(ar.active)ar.aborted++;
   ar.active=ar.pending=false;return false;
  }
 }
 uint64_t hardware=nowStamp();
 if(ar.active&&(mulTimestampDifference(hardware,ar.end)>=0||mulTimestampDifference(hardware,ar.deadline)>=0)){cancel();return true;}
 if(status&(SYS_STATUS_ALL_RX_ERR|SYS_STATUS_ALL_RX_TO|DWT_INT_RXOVRR_BIT_MASK)){if(ar.active){cancel();return true;}return false;}
 if(status&DWT_INT_TXFRS_BIT_MASK) {
  if(!ar.pending){ar.rejected++;cancel();return true;}
  uint8_t tx[5];dwt_readtxtimestamp(tx);dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);
  if(mulDecodeTimestamp(tx)!=ar.expected){cancel();return true;}
  ar.pending=false;ar.stage=ar.stage==mulTwrResponse?mulTwrFinal:mulTwrResult;
 }
 if(status&DWT_INT_RXFCG_BIT_MASK) {
  unsigned n=dwt_read_reg(RX_FINFO_ID)&RX_FINFO_RXFLEN_BIT_MASK;
  if(n==MUL_MULTI_TWR_BYTES+2) {
   uint8_t frame[MUL_MULTI_TWR_BYTES],t[5];dwt_readrxdata(frame,sizeof(frame),0);dwt_readrxtimestamp(t,DWT_COMPAT_NONE);
   receive(frame,mulDecodeTimestamp(t),now);return true;
  }
  if(ar.active){cancel();return true;}
 }
 if(ar.retry)rearm();
 return ar.active;
}
#endif
