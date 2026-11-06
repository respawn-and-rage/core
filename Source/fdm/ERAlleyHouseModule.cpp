#include "ERProceduralMap.h"
#include "ERMeshCells.h"

namespace ERProcedural
{
void AddAlleyHouseModule(const UERMapTemplate& T,const FTransform& Frame,FVector Size,bool RoofAccess,const TArray<int32>& StreetFaces,FERProceduralLayout& L)
{
    auto& House=L.Houses.AddDefaulted_GetRef();
    House.Frame=FTransform(Frame.GetRotation(),Frame.GetLocation(),Size);House.RoofAccess=RoofAccess;
    House.StructureName=FName(*FString::Printf(TEXT("AlleyModule_%d"),L.Houses.Num()-1));
    House.RimHeight=((L.Houses.Num()-1)/2)%3==0 ? 0:25;
    for(int32 Face:StreetFaces)
    {
        FVector Outside(0,0,115),Inside=Outside;
        Outside[Face/2]=(Face%2 ? 1:-1)*(Size[Face/2]*.5+85);
        Inside[Face/2]=(Face%2 ? 1:-1)*(Size[Face/2]*.5-140);
        House.Entrances.Add({Frame.TransformPosition(Outside),Frame.TransformPosition(Inside),Frame.TransformPosition(FVector(0,0,115))});
        House.EntranceFaces.Add(Face);
    }
    if(RoofAccess)House.RoofMouths.Add(Frame.TransformPosition({-Size.X*.5,-Size.Y*.5+140,Size.Z}));
}

void FinishAlleyHouseModules(const UERMapTemplate& T,const TArray<FERMapTemplatePiece>& Pieces,FERProceduralLayout& L)
{
    // Finalize only after terrain, towers, bridge and city walls are known.
    // Doorways are emitted where an actual supported street approach exists.
    constexpr float Floor=15,Wall=20,DoorWidth=160,DoorHeight=240,Rim=40;
    for(int32 Index=0;Index<L.Houses.Num();++Index)
    {
    auto& House=L.Houses[Index];const auto Size=House.Frame.GetScale3D();const float Cap=House.RimHeight;
    const FTransform Frame(House.Frame.GetRotation(),House.Frame.GetLocation());
    for(int32 I=House.Entrances.Num()-1;I>=0;--I)
    {
        auto& Entry=House.Entrances[I];const float Ground=AlleyGroundHeight(L,FVector2D(Entry[0]));
        bool Clear=FMath::Abs(Ground-(Frame.GetLocation().Z+Floor))<=28;
        Entry[0].Z=Ground+100;
        for(const auto& Hole:L.UnderpassBreaches)Clear&=!Hole.ExpandBy(FVector(40,40,1000)).IsInsideOrOn(Entry[0]);
        for(const auto& Ramp:L.UnderpassRamps)Clear&=!Ramp.Bounds.ExpandBy(FVector(40,40,1000)).IsInsideOrOn(Entry[0]);
        for(const auto& B:Pieces)if(!B.VisualOnly)
        {
            const FTransform Local(FRotator(0,B.RotationYaw,0),B.Center);
            const FBox Bounds=FBox(-B.Size*.5,B.Size*.5).ExpandBy(FVector(35,35,93));
            const auto A=Local.InverseTransformPosition(Entry[0]+FVector(0,0,20)),End=Local.InverseTransformPosition(Entry[1]+FVector(0,0,20));
            if(FMath::LineBoxIntersection(Bounds,A,End,End-A)){Clear=false;break;}
        }
        if(!Clear){House.Entrances.RemoveAt(I);House.EntranceFaces.RemoveAt(I);}
    }
    auto& P=L.Panels.AddDefaulted_GetRef();P.Id=100001+Index;
    P.Material=EERBlockMaterial::Brick;
    P.Surface=T.Town.WoodSurface;P.Size=Size+FVector(0,0,Cap);
    P.Transform=FTransform(Frame.GetRotation(),Frame.TransformPosition(-FVector(Size.X,Size.Y,0)*.5));
    P.Cell={FERCellWorld::FitCell(P.Size.X,5),FERCellWorld::FitCell(P.Size.Y,5),FERCellWorld::FitCell(P.Size.Z,5)};
    P.bAnchorBottom=true;P.SkinName=House.StructureName;
    auto Box=[&](FVector Min,FVector Max)
    {
        if((Max-Min).GetMin()<.01)return;
        FERCellPanelDesc Part;Part.Size=Max-Min;ERMeshCells::BoxSkin(Part);
        for(auto Corner:Part.Skin){Corner.Position+=Min;P.Skin.Add(Corner);}
    };
    TArray<FERMeshCorner> Slabs[2];
    Box(FVector::ZeroVector,{Size.X,Size.Y,Floor});Slabs[0]=MoveTemp(P.Skin);
    Box({0,0,Size.Z-20},Size);Slabs[1]=MoveTemp(P.Skin);
    P.Skin.Reset();
    for(int32 Face=0;Face<4;++Face)
    {
        const int32 Axis=Face/2,Along=1-Axis;const bool Positive=(Face%2)!=0;
        const float Length=Size[Along]-(Axis==1 ? 2*Wall:0),Start=Axis==1 ? Wall:0;
        if(Cap>0)
        {
            const float RimStart=Axis==1 ? Rim:0,RimEnd=Size[Along]-RimStart;
            TArray<FVector2D> Gaps;
            for(const auto& Mouth:House.RoofMouths)
            {
                const auto M=Frame.InverseTransformPosition(Mouth)+FVector(Size.X,Size.Y,0)*.5;
                if(FMath::Abs(M[Axis]-(Positive ? Size[Axis]:0))<=60)
                    Gaps.Add({FMath::Max(double(RimStart),M[Along]-90),FMath::Min(double(RimEnd),M[Along]+90)});
            }
            Gaps.Sort([](const auto& A,const auto& B){return A.X<B.X;});
            double Edge=RimStart;
            auto Segment=[&](double A,double B)
            {
                if(B<=A)return;FVector Min=FVector::ZeroVector,Max=Size;
                Min[Axis]=Positive ? Size[Axis]-Rim:0;Max[Axis]=Min[Axis]+Rim;
                Min[Along]=A;Max[Along]=B;Min.Z=Size.Z;Max.Z=Size.Z+Cap;Box(Min,Max);
            };
            for(const auto& Gap:Gaps){Segment(Edge,Gap.X);Edge=FMath::Max(Edge,Gap.Y);}Segment(Edge,RimEnd);
        }
        TArray<FBox2D> Holes;
        if(House.EntranceFaces.Contains(Face))Holes.Add(FBox2D({Length*.5-DoorWidth*.5,Floor},{Length*.5+DoorWidth*.5,Floor+DoorHeight}));
        // Window jambs stay substantial even in the smallest frontage. Upper
        // windows keep the sand/plaster silhouette without internal obstacles.
        if(Length>=700)for(float U:{Length*.20f,Length*.80f})
            Holes.Add(FBox2D({U-65,Floor+100},{U+65,Floor+220}));
        if(Size.Z>=600 && Length>=450)
            Holes.Add(FBox2D({Length*.5-70,Size.Z-220},{Length*.5+70,Size.Z-100}));
        TArray<double> X{0,Length},Z{Floor,Size.Z-20};
        for(const auto& H:Holes){X.AddUnique(H.Min.X);X.AddUnique(H.Max.X);Z.AddUnique(H.Min.Y);Z.AddUnique(H.Max.Y);}
        X.Sort();Z.Sort();
        for(int32 I=1;I<X.Num();++I)for(int32 J=1;J<Z.Num();++J)
        {
            const FVector2D Mid((X[I-1]+X[I])*.5,(Z[J-1]+Z[J])*.5);
            if(Holes.ContainsByPredicate([&](const auto& H){return H.IsInside(Mid);}))continue;
            FVector Min=FVector::ZeroVector,Max=Size;
            Min[Axis]=Positive ? Size[Axis]-Wall:0;Max[Axis]=Min[Axis]+Wall;
            Min[Along]=Start+X[I-1];Max[Along]=Start+X[I];Min.Z=Z[J-1];Max.Z=Z[J];Box(Min,Max);
        }
    }
    // Hash the actual generated corners, including apertures and the rim.
    P.SkinCRC=1;
    for(const auto& C:P.Skin)for(int32 Axis=0;Axis<3;++Axis)
        P.SkinCRC=HashCombineFast(P.SkinCRC,GetTypeHash(FMath::RoundToInt(C.Position[Axis]*100)));
    // Separate panel skins retain destructive geometry while assigning slab finishes independently.
    const FERCellPanelDesc Base=P;
    P.Transform.AddToTranslation(P.Transform.TransformVectorNoScale(FVector(0,0,Floor)));
    P.Size.Z-=Floor;P.Cell.Z=FERCellWorld::FitCell(P.Size.Z,5);
    for(auto& Corner:P.Skin)Corner.Position.Z-=Floor;
    for(int32 Slab=0;Slab<2;++Slab)
    {
        auto& Deck=L.Panels.AddDefaulted_GetRef();Deck=Base;
        Deck.Id=200001+Index*2+Slab;Deck.Skin=MoveTemp(Slabs[Slab]);
        const float Bottom=Slab ? Size.Z-20:0,Thickness=Slab ? 20:Floor;
        Deck.Transform.AddToTranslation(Deck.Transform.TransformVectorNoScale(FVector(0,0,Bottom)));
        Deck.bAnchorBottom=Slab==0;Deck.Size.Z=Thickness;Deck.Cell.Z=FERCellWorld::FitCell(Thickness,5);
        for(auto& Corner:Deck.Skin)Corner.Position.Z-=Bottom;
        Deck.SkinName=FName(*(House.StructureName.ToString()+(Slab ? TEXT("_Roof"):TEXT("_Floor"))));
        Deck.Surface=T.Town.RoofSurface;Deck.SkinCRC=HashCombineFast(Base.SkinCRC,Slab+1);
    }
    }
}
}
