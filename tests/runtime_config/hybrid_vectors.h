/* User-supplied2026-10-22 independent serial snapshots, NOT a full exchange.
 * Decode and validate with the actual shared C codecs; no inferred RX is data. */
static void hardware_vectors(void) {
static const char *const frames[]={
"41DC5FCFBCFFFFFFFFFFFFFFFF840000000000CFBC4D554C015F0B0000A2087FCF33",
"41DC4ECFBCFFFFFFFFFFFFFFFF850000000000CFBC4D554C024E0B0000A290D5F1F8021701000000000000005E0B0000590B0000A208E7EE24888F8981F1C5070000F6010F137E007017",
"41DC5ECFBC850000000000CFBC840000000000CFBC4D554C145E0B0000A2087D9A2E1FD3202DFBA2D44C79FB3601A9E62EA202D5322F0CE178C5FBA2FC2CCB2F840000000189E512010230E511010249E5120102"};

 mulNetwork_t historical=mulDefaultNetwork;historical.slotMs=500;
 uint8_t b[84];mulTestPacket_t p;mulRelation_t r;uint64_t nextReference;
 for(unsigned k=0;k<3;k++) {
   unsigned n=k==0?34:k==1?74:84;
   assert(strlen(frames[k])==2*n);
   for(unsigned i=0;i<n;i++){unsigned value;assert(sscanf(frames[k]+2*i,"%2x",&value)==1);b[i]=(uint8_t)value;}
   if(k==0){assert(mulBroadcastDecode(b,n,&p,&r));assert(p.sequence==2911&&p.txTimestamp==UINT64_C(0x33CF7F08A2));nextReference=p.txTimestamp;}
   else if(k==1) {
     assert(mulBroadcastDecode(b,n,&p,&r));
     assert(p.sequence==2894&&r.referenceSequence==2910&&r.twrSequence==2905&&r.epoch==1);
     assert(r.available&&r.skewPpb==1989&&r.originAgeMs==502&&r.delayAgeMs==4879&&r.delayDtu==126&&r.remainingMs==1120);
     uint64_t mapped;assert(mulRelationInverse(&r,p.txTimestamp,1,&mapped));
     assert(mapped==UINT64_C(0x2C5F321182)&&mulTimestampDifference(nextReference,r.referenceOrigin)==INT64_C(63897600000));
     assert(!mulRelationInverse(&r,p.txTimestamp,1121,&mapped));
     /* Test binding independently using synthetic observation containers, not
      * inventing the missing hardware RX of reference2910. RX is not used by
      * this API: full geometric TDoA and receiver rate remain unavailable. */
     mulPair_t pair={0};pair.reference.valid=pair.follower.valid=true;
     pair.reference.acceptedMs=323487;pair.follower.acceptedMs=323987;
     pair.reference.source.sequence=2910;pair.reference.source.txTimestamp=r.referenceOrigin;
     pair.follower.source.txTimestamp=p.txTimestamp;
     memcpy(pair.reference.sourceClock,(uint8_t[]){0x84,0,0,0,0,0,0xcf,0xbc},8);
     memcpy(pair.follower.sourceClock,p.source,8);int64_t dt;
     assert(!mulDownlinkRelationNetwork(&historical,&r,&pair,323987,65535,&dt));
     assert(mulDownlinkRelationNetwork(&historical,&r,&pair,323987,1,&dt)&&dt==INT64_C(31948736736));
     pair.reference.source.sequence=2911;assert(!mulDownlinkRelationNetwork(&historical,&r,&pair,323987,1,&dt));
     pair.reference.source.sequence=2910;assert(!mulDownlinkRelationNetwork(&historical,&r,&pair,325108,1,&dt));
   } else {
     mulTwrPacket_t t;uint32_t raw,corrected;assert(mulTwrDecode(b,n,&t));
     assert(t.type==mulTwrResult&&t.id==2910&&t.tof==132);
     assert(mulTwrResults(t.stamps,t.power,&raw,&corrected)&&raw==114&&corrected==132);
     /* Hearing RESULT alone is not a verified complete passive exchange. */
     mulHybrid_t listener;mulHybridOutput_t out;uint8_t receiver[8]={0x8d,0,0,0,0,0,0xcf,0xbc};
     mulHybridInit(&listener,receiver,false);
     assert(mulHybridReceive(&listener,b,n,UINT64_C(0xB4CE5F1003),1,324217,&out));
     assert(!out.twrComplete&&listener.diagnostics.lastReason==mulHybridNoReference);
   }
 }
}
