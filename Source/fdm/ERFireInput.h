#pragma once
#include "CoreMinimal.h"

namespace ERFirePrediction
{
    inline bool Ready(double LocalNow,double NextLocal,uint16 Ordinal,double ServerNow,double LastServerShot,double Interval)
    {
        // Replication can update the accepted clock in the middle of a locally
        // paced burst. Only a new press consults the authoritative cooldown.
        return LocalNow>=NextLocal && (Ordinal>0 || ServerNow>=LastServerShot+Interval);
    }
}

// Captured once at the press, never refreshed by a later move or release.
// Revision equality also rejects A -> B -> A while the original press is delayed.
struct FERFireWeaponBinding
{
    uint8 Slot=MAX_uint8;
    uint16 Revision=0;
    bool Matches(int32 SelectedSlot,uint16 SelectedRevision) const
    {return SelectedSlot>=0 && SelectedSlot<MAX_uint8 && Slot==SelectedSlot && Revision==SelectedRevision;}
    bool operator==(const FERFireWeaponBinding& Other) const {return Slot==Other.Slot && Revision==Other.Revision;}
    bool operator!=(const FERFireWeaponBinding& Other) const {return !(*this==Other);}
};

// The press counter travels on every saved move, including after release. A tap
// therefore survives a lost press packet without a reliable RPC per click.
struct FERFireInputState
{
    uint16 Sequence=0;
    bool Held=false;
    static bool Newer(uint16 A,uint16 B) { return A!=B && uint16(A-B)<0x8000; }
    bool Receive(uint16 Incoming,bool Down,bool& Pressed)
    {
        Pressed=false;
        if(Incoming!=Sequence && !Newer(Incoming,Sequence))return false;
        Pressed=Incoming!=Sequence;
        const bool Changed=Pressed || Down!=Held;
        Sequence=Incoming;Held=Down;
        return Changed;
    }
};

// Presentation identities are unrelated to gameplay RNG or weapon recoil.
// Keep a bounded recent window even after an ack: an older multicast may follow it.
struct FERShotPresentationWindow
{
    static constexpr int32 Capacity=128;
    TArray<uint32,TInlineAllocator<Capacity>> Keys;
    int32 Next=0;
    static uint32 Key(uint16 Sequence,uint16 Ordinal) { return (uint32(Sequence)<<16)|Ordinal; }
    bool Mark(uint16 Sequence,uint16 Ordinal)
    {
        if(Ordinal==0)return true; // Legacy authority-only callers have no prediction identity.
        const uint32 Value=Key(Sequence,Ordinal);
        if(Keys.Contains(Value))return false;
        if(Keys.Num()<Capacity)Keys.Add(Value);
        else {Keys[Next]=Value;Next=(Next+1)%Capacity;}
        return true;
    }
};
