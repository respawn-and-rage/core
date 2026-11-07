#pragma once
#include "CoreMinimal.h"

// A summed-area mask makes fully explored interior squares one quad. Boundary
// cells keep the original clipping/feathering, and unknown cells never join a quad.
struct FERMinimapFog
{
    static constexpr int32 Grid=64;
    TArray<uint8> Mask;
    TArray<int32> Prefix;
    void Update(const TArray<uint8>& Bits)
    {
        if(Mask==Bits && Prefix.Num()==(Grid+1)*(Grid+1))return;
        Mask=Bits;Prefix.Init(0,(Grid+1)*(Grid+1));
        for(int32 Y=0;Y<Grid;++Y)for(int32 X=0;X<Grid;++X)
        {
            const int32 FogCell=Y*Grid+X;
            const int32 Seen=Bits.IsValidIndex(FogCell/8) && (Bits[FogCell/8]&(1<<(FogCell%8)))!=0 ? 1:0;
            Prefix[(Y+1)*(Grid+1)+X+1]=Seen+Prefix[Y*(Grid+1)+X+1]+Prefix[(Y+1)*(Grid+1)+X]-Prefix[Y*(Grid+1)+X];
        }
    }
    int32 Count(int32 X,int32 Y,int32 Size) const
    {
        const int32 Pitch=Grid+1;
        return Prefix[(Y+Size)*Pitch+X+Size]-Prefix[Y*Pitch+X+Size]-Prefix[(Y+Size)*Pitch+X]+Prefix[Y*Pitch+X];
    }
    template<typename TVisit>
    void Visit(FVector2D Origin,double MapScale,double InnerRadius,TVisit&& Emit) const
    {
        if(Prefix.Num()!=(Grid+1)*(Grid+1))return;
        const auto Walk=[&](auto&& Self,int32 X,int32 Y,int32 Size)->void
        {
            const int32 Seen=Count(X,Y,Size);
            if(!Seen)return;
            const FVector2D Lo=(FVector2D(double(X)/Grid-.5,double(Y)/Grid-.5)-Origin)*(2*MapScale);
            const FVector2D Hi=Lo+FVector2D(double(Size)/Grid*2*MapScale);
            const FVector2D Nearest(FMath::Clamp(0.,Lo.X,Hi.X),FMath::Clamp(0.,Lo.Y,Hi.Y));
            if(Nearest.SizeSquared()>1.000001)return;
            const FVector2D Farthest(FMath::Max(FMath::Abs(Lo.X),FMath::Abs(Hi.X)),FMath::Max(FMath::Abs(Lo.Y),FMath::Abs(Hi.Y)));
            if(Size==1 || (Seen==Size*Size && Farthest.SizeSquared()<=InnerRadius*InnerRadius))
            {Emit(X,Y,Size);return;}
            const int32 Half=Size/2;
            Self(Self,X,Y,Half);Self(Self,X+Half,Y,Half);
            Self(Self,X,Y+Half,Half);Self(Self,X+Half,Y+Half,Half);
        };
        Walk(Walk,0,0,Grid);
    }
};
