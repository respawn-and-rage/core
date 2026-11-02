#include "ERProceduralMap.h"
#include "ERRoomsMacro.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Materials/MaterialInterface.h"
#include "Algo/Reverse.h"

namespace ERProcedural
{
float RoomsFloorHeight(const FERProceduralLayout& L,FVector2D P)
{
    for(const auto& V:L.RoomNodes)
    {
        const FVector2D Local=P-FVector2D(V.Center);bool Inside=true;
        for(int32 I=0;I<V.Polygon.Num();++I)
        {
            const auto A=V.Polygon[I],B=V.Polygon[(I+1)%V.Polygon.Num()];
            if(FVector2D::CrossProduct(B-A,Local-A)<-.01){Inside=false;break;}
        }
        if(Inside)return V.Center.Z;
    }
    for(const auto& E:L.RoomLinks)
    {
        const FVector2D A(E.Start),D=FVector2D(E.End)-A;const double T=FVector2D::DotProduct(P-A,D)/D.SizeSquared();
        if(T>=0 && T<=1 && FVector2D::DistSquared(P,A+D*T)<=FMath::Square(E.Width*.5f))return FMath::Lerp(E.Start.Z,E.End.Z,T);
    }
    return 0;
}
bool BuildRooms(const FERProceduralConfig& C,const FERBlockRoomConfig& B,FERProceduralLayout& Out,const FERCellSettings& Cells)
{
    const auto* T=C.TemplateAsset.Get();if(!T)return false;
    const auto& R=T->Rooms;
    if(T->TemplateId.IsEmpty() || T->Revision<1 || T->Symmetry!=EERTemplateSymmetry::HalfTurn || C.Floors!=1 ||
       B.BlockSize!=FVector(50,20,25) || !FMath::IsFinite(B.ChunkSize) || B.ChunkSize<100 ||
       R.MinLaneRooms<1 || R.MaxLaneRooms>4 || R.MinLaneRooms>R.MaxLaneRooms ||
       !FMath::IsFinite(R.CorridorWidth) || R.CorridorWidth<320 || R.CorridorWidth>400 ||
       !FMath::IsFinite(R.CorridorHeight) || R.CorridorHeight<300 || R.CorridorHeight>700 ||
       !FMath::IsFinite(R.CornerCut) || R.CornerCut<30 || R.CornerCut>65 || R.Themes.Num()!=3 ||
       T->Town.Enabled || T->Variation.Enabled || !T->Pieces.IsEmpty() || T->RoofMesh || T->ShellWallMesh)return false;
    for(const auto& Theme:R.Themes)if(Theme.Name.IsNone() || !Theme.Wall || !Theme.Floor || !Theme.Ceiling || !Theme.Wood)return false;
    FRandomStream Geometry(C.Seed),Cosmetic(int32(HashCombine(uint32(C.Seed),0x524f4f4du)));
    FERProceduralLayout L;L.RoomsTheme=Cosmetic.RandRange(0,2);
    const auto& Theme=R.Themes[L.RoomsTheme];L.GroundSurface=Theme.Floor;
    const int32 N=Geometry.RandRange(R.MinLaneRooms,R.MaxLaneRooms);
    bool HallPassage=false;int32 HallStyle=0;
    const float S=R.NominalRoomSize>0 ? R.NominalRoomSize:N==2 ? 2200.f:N==3 ? 2000.f:1800.f;
    if(!FMath::IsFinite(R.NominalRoomSize) || R.NominalRoomSize<0 || !FMath::IsFinite(S) || S<800 || S>2400 || !FMath::IsFinite(R.LaneSpacing) || R.LaneSpacing<800 || R.LaneSpacing>2400 ||
       !FMath::IsFinite(R.LaneOffset) || R.LaneOffset<400 || R.LaneOffset>1700 || !FMath::IsFinite(R.PlanMargin) || R.PlanMargin<300 || R.PlanMargin>1100)return false;
    const bool GroundOnly=T->SizingRoomsGroundOnly || N==1;
    const float X=S+R.LaneOffset,Y=N==1 ? S+R.LaneSpacing:(N-1)*(S+R.LaneSpacing)*.5f,H=FMath::Max(560.f,R.CorridorHeight);
    auto Turn=[](FVector P){return FVector(-P.X,-P.Y,P.Z);};
    auto Outline=[](FVector2D Size,float Cut)
    {
        const float A=Size.X*.5f,D=Size.Y*.5f;
        return Cut>0 ? TArray<FVector2D>{{-A+Cut,-D},{A-Cut,-D},{A,-D+Cut},{A,D-Cut},{A-Cut,D},{-A+Cut,D},{-A,D-Cut},{-A,-D+Cut}}:
            TArray<FVector2D>{{-A,-D},{A,-D},{A,D},{-A,D}};
    };
    auto Node=[&](FVector Center,FVector2D Size,float Cut,bool Base=false,bool Junction=false,bool Upper=false)
    {
        FERRoomsNode V;V.Center=Center;V.Size=Size;V.Base=Base;V.Junction=Junction;V.Upper=Upper;V.Height=Upper ? 1040:H;
        V.Polygon=Outline(Size,Cut);
        return L.RoomNodes.Add(MoveTemp(V));
    };
    Node({0,-Y,0},{S+200,S},150,true);Node({0,Y,0},{S+200,S},150,true);
    const int32 UpperRow=(N-1)/2;
    for(int32 Row=0;Row<N;++Row)
    {
        const float Bend=Row==UpperRow || (T->SizingTeamSize>0 && R.PlanMargin<1000) ? 0.f:float((Row%2 ? -1:1)*(R.NominalRoomSize>0 ? 800.f:800.f));
        const float Floor=GroundOnly ? 0.f:T->SizingTeamSize>0 && S<=1600 ? 320.f:Row==UpperRow ? 320.f:float(Geometry.RandRange(0,2)*160);
        const FVector2D Size(FMath::Max(800.f,S+Geometry.RandRange(-2,2)*100),FMath::Max(800.f,S+Geometry.RandRange(-2,2)*100));
        const float Cut=Geometry.RandRange(0,2)==0 ? 0:FMath::Min(float(Geometry.RandRange(2,4)*40),float((Size.GetMin()-R.CorridorWidth)*.5-40));
        Node({-X+Bend,N==1 ? 0.f:-Y+Row*(S+R.LaneSpacing),Floor},
            Size,Cut,false,false,!GroundOnly && (Row==0 || Row==UpperRow));
    }
    // Copy a complete half, including polygon shape, elevation and upper gallery.
    for(int32 Row=0;Row<N;++Row)
    {
        auto V=L.RoomNodes[2+N-1-Row];V.Center=Turn(V.Center);
        for(auto& P:V.Polygon)P=-P;
        L.RoomNodes.Add(MoveTemp(V));
    }
    int32 LeftMid=2+N/2,RightMid=2+N+N/2;
    if(N%2==0)
    {
        LeftMid=Node({-X,0,GroundOnly ? 0.f:T->SizingTeamSize>0 && S<=1600 ? 320.f:160.f},{400,400},0,false,true);
        RightMid=Node({X,0,GroundOnly ? 0.f:T->SizingTeamSize>0 && S<=1600 ? 320.f:160.f},{400,400},0,false,true);
    }
    TArray<int32> Partners;Partners.SetNum(L.RoomNodes.Num());
    for(int32 I=0;I<Partners.Num();++I)
        Partners[I]=I<2 ? 1-I:I<2+N ? 2+N+(N-1-(I-2)):I<2+2*N ? 2+(N-1-(I-2-N)):I==LeftMid ? RightMid:LeftMid;
    auto Mirror=[&](int32 I){return Partners[I];};
    const int32 AlphaBase=0,OmegaBase=Mirror(AlphaBase);
    if(N==1)
    {
        // Four corners, with the bases on opposite diagonals. Every room is
        // a through-room on one of the two attack paths; there are no stubs.
        L.RoomNodes[0].Center={-X,-Y,0};L.RoomNodes[1].Center={X,Y,0};
        L.RoomNodes[2].Center={X,-Y,0};L.RoomNodes[3].Center={-X,Y,0};
        if(T->SizingTeamSize>0)for(int32 BaseIndex:{AlphaBase,OmegaBase})
            L.RoomNodes[BaseIndex].Polygon=Outline(L.RoomNodes[BaseIndex].Size,100);
        const auto BaseSize=L.RoomNodes[0].Size;
        const float HX=X+S*.5f+R.PlanMargin,HY=Y+S*.5f+200;
        const float Cut=L.RoomNodes[2].Polygon.Num()==8 ? L.RoomNodes[2].Polygon[0].X+L.RoomNodes[2].Size.X*.5f:0;
        FRandomStream Shape(int32(HashCombine(uint32(C.Seed),0x48414c4cu)));
        const bool Wide=Shape.RandRange(0,1)!=0;
        const int32 PreferredStyle=Shape.RandRange(0,2);const float DesiredGap=Shape.RandRange(2,5)*100.f;
        const float DesiredInset=Shape.RandRange(2,4)*100.f;
        HallPassage=(uint32(C.Seed)&1)==0;
        bool Fitted=false;
        for(int32 Try=0;Try<4 && !Fitted;++Try)
        {
            const bool Horizontal=Wide!=((Try%2)!=0);
            const int32 Style=Try<2 ? PreferredStyle:0;
            const float Gap=Try<2 ? DesiredGap:200;
            const float Inset=Style==1 ? FMath::Max(0.f,FMath::Min3(DesiredInset,S*.5f-R.CorridorWidth*.5f-60-FMath::Max(Cut,80.f),S*.5f+R.PlanMargin-200-R.CorridorWidth*.5f-60-FMath::Max(Cut,80.f))):0;
            const FVector2D Min(Horizontal ? -X+BaseSize.X*.5f+600:Gap*.5f,-HY+200+Inset);
            const FVector2D Max(HX-200-Inset,Horizontal ? -Gap*.5f:Y-BaseSize.Y*.5f-600),Size=Max-Min;
            const float AlongX=Min.X-(-X+BaseSize.X*.5f),AlongY=Y-BaseSize.Y*.5f-Max.Y;
            const float MouthCut=HallPassage ? FMath::Min(Cut,float(Horizontal ? -Min.X:Max.Y)-R.CorridorWidth*.5f-60):Cut;
            if(MouthCut<0)continue;
            TArray<FVector2D> Polygon=Outline(Size,MouthCut);
            if(Style==2)
            {
                const float Door=R.CorridorWidth*.5f+60;
                const float Big=FMath::Min(float(Size.GetMin())*.24f,600.f);
                float NW=FMath::Min(Big,float(-Y-Min.Y)-Door),NE=Big,SE=FMath::Min(Big,float(Max.X-X)-Door);
                float SW=FMath::Min3(Big,float(Max.Y+Y)-Door,float(X-Min.X)-Door);
                if(HallPassage)
                {
                    if(Horizontal)SW=FMath::Min(SW,float(-Min.X)-Door);
                    else {NW=FMath::Min(NW,float(-Min.Y)-Door);SW=FMath::Min(SW,float(Max.Y)-Door);}
                }
                if(FMath::Min3(NW,SE,SW)<40)continue;
                const float A=Size.X*.5f,D=Size.Y*.5f;
                Polygon={{-A+NW,-D},{A-NE,-D},{A,-D+NE},{A,D-SE},{A-SE,D},{-A+SW,D},{-A,D-SW},{-A,-D+NW}};
            }
            double Twice=0;for(int32 I=0;I<Polygon.Num();++I)Twice+=FVector2D::CrossProduct(Polygon[I],Polygon[(I+1)%Polygon.Num()]);
            const float Floor=2*(BaseSize.X*BaseSize.Y-45000)+FMath::Abs(Twice)+(2*(AlongX+AlongY)+(HallPassage ? Gap:0))*(R.CorridorWidth-2*R.CornerCut);
            const float Minimum=Style==0 ? .6f:.55f;
            if(Size.GetMin()<800 || AlongX<600 || AlongY<600 || Floor<4*HX*HY*Minimum)continue;
            auto& Hall=L.RoomNodes[2];Hall.Center=FVector((Min+Max)*.5,0);Hall.Size=Size;Hall.Polygon=MoveTemp(Polygon);
            L.RoomNodes[3]=Hall;L.RoomNodes[3].Center=Turn(Hall.Center);for(auto& P:L.RoomNodes[3].Polygon)P=-P;
            HallStyle=Style;
            Fitted=true;
        }
        if(!Fitted)return false;
    }
    auto JointPair=[&](FVector P)
    {
        const int32 A=Node(P,{440,440},0,false,true),D=Node(Turn(P),{440,440},0,false,true);
        Partners.Add(D);Partners.Add(A);return A;
    };
    auto Segment=[&](int32 A,int32 D,float RaiseA=0,float RaiseD=0)
    {
        const auto& NA=L.RoomNodes[A];const auto& ND=L.RoomNodes[D];
        if(N==1)
        {
            // The expanded halls have off-centre mouths, aligned with their
            // original corner bases so both connecting corridors stay straight.
            const bool Across=(A==0 && D==2) || (A==1 && D==3);
            const float Sign=FMath::Sign(Across ? ND.Center.X-NA.Center.X:ND.Center.Y-NA.Center.Y);
            const FVector Start=NA.Center+(Across ? FVector(Sign*NA.Size.X*.5f,0,RaiseA):FVector(0,Sign*NA.Size.Y*.5f,RaiseA));
            const FVector End=Across ? FVector(ND.Center.X-Sign*ND.Size.X*.5f,NA.Center.Y,ND.Center.Z+RaiseD):FVector(NA.Center.X,ND.Center.Y-Sign*ND.Size.Y*.5f,ND.Center.Z+RaiseD);
            L.RoomLinks.Add({A,D,Start,End,R.CorridorWidth,H,R.CornerCut});return;
        }
        const FVector Dir=(ND.Center-NA.Center).GetSafeNormal2D();const bool Across=FMath::Abs(Dir.X)>.5;
        const float RA=(Across ? NA.Size.X:NA.Size.Y)*.5f,RD=(Across ? ND.Size.X:ND.Size.Y)*.5f;
        L.RoomLinks.Add({A,D,NA.Center+Dir*RA+FVector(0,0,RaiseA),ND.Center-Dir*RD+FVector(0,0,RaiseD),R.CorridorWidth,H,R.CornerCut});
    };
    auto PairSegment=[&](int32 A,int32 D,float RaiseA=0,float RaiseD=0)
    {Segment(A,D,RaiseA,RaiseD);Segment(Mirror(A),Mirror(D),RaiseA,RaiseD);};
    auto LinkPair=[&](int32 A,int32 D)
    {
        const FVector P=L.RoomNodes[A].Center,Q=L.RoomNodes[D].Center;
        if(FMath::Abs(P.X-Q.X)<1 || FMath::Abs(P.Y-Q.Y)<1){PairSegment(A,D);return;}
        // Axis-aligned mouths and two real turning chambers make a dogleg,
        // rather than overlapping diagonal shells at the room doors.
        const float Direction=FMath::Sign(Q.Y-P.Y);
        const float Mid=(P.Y+Direction*L.RoomNodes[A].Size.Y*.5f+Q.Y-Direction*L.RoomNodes[D].Size.Y*.5f)*.5f,Z=(P.Z+Q.Z)*.5f;
        const int32 J=JointPair({P.X,Mid,Z}),K=JointPair({Q.X,Mid,Z});
        PairSegment(A,J);PairSegment(J,K);PairSegment(K,D);
    };
    TArray<int32> Lane{0};
    for(int32 Row=0;Row<N;++Row){Lane.Add(2+Row);if(N%2==0 && Row==N/2-1)Lane.Add(LeftMid);}
    Lane.Add(1);
    int32 SpineA=0,SpineB=1;
    if(N==1)
    {
        PairSegment(0,2);PairSegment(0,3);
    }
    else for(int32 I=1;I<Lane.Num();++I)LinkPair(Lane[I-1],Lane[I]);
    // A direct lower spine passes under an elevated transverse route. The
    // upper route enters actual room galleries, reached by their switchbacks.
    int32 UpperLeft=2+UpperRow,UpperRight=Mirror(UpperLeft);
    if(!GroundOnly && N%2==0)
    {
        const int32 J=JointPair({-X,0,940});
        PairSegment(UpperLeft,J,620,0);UpperLeft=J;UpperRight=Mirror(J);
    }
    // Baked PCGEx plans replace the straight lower spine with a central structure. What does
    // not fit this seed's geometry is dropped, and the last resort is the legacy spine.
    int32 MacroSelfLinks=0;
    auto ApplyMacro=[&](const FERRoomsMacroVariant& Variant,bool UseBaseLinks,bool UseRungs)
    {
        namespace Macro=ERRoomsMacro;
        const int32 FirstNode=L.RoomNodes.Num(),FirstLink=L.RoomLinks.Num(),FirstPartner=Partners.Num();
        auto Fail=[&]{L.RoomNodes.SetNum(FirstNode);L.RoomLinks.SetNum(FirstLink);Partners.SetNum(FirstPartner);return false;};
        for(const FIntPoint& Link:Variant.Links)if(!Macro::IsRealizable(Link,N))return Fail();
        const float Offset=Macro::CentreOffset(X);
        auto LaneNode=[&](bool Left){return N%2 ? (Left ? 2+N/2:2+N+N/2):(Left ? LeftMid:RightMid);};
        // At base height: compact plans have no run left for a ramp before the centre.
        const int32 CentreL=JointPair({-Offset,0,0}),CentreR=Mirror(CentreL);
        const bool Across=Variant.Links.Contains(FIntPoint(Macro::CentreLeft,Macro::CentreRight));
        // One base mouth: when the graph keeps both directions, the left one stands.
        const bool ToLeft=Variant.Links.Contains(FIntPoint(Macro::AlphaBase,Macro::CentreLeft));
        const bool ToRight=Variant.Links.Contains(FIntPoint(Macro::AlphaBase,Macro::CentreRight));
        if(!ToLeft && !ToRight)return Fail();
        if(UseBaseLinks)LinkPair(0,ToLeft ? CentreL:CentreR);
        const int32 Middle=Macro::LaneMiddle(true,N);
        const bool Rungs=UseRungs && Variant.Links.Contains(FIntPoint(FMath::Min<int32>(Middle,Macro::CentreLeft),FMath::Max<int32>(Middle,Macro::CentreLeft)));
        if(Rungs)PairSegment(LaneNode(true),CentreL);
        // Each centre needs a way through, and must join the map on both sides.
        if((UseBaseLinks && !Across && !Rungs) || (!UseBaseLinks && (!Across || !Rungs)))return Fail();
        const int32 Pairs=L.RoomLinks.Num();
        if(Across)Segment(CentreL,CentreR);
        // Same rules as the corridors around them: walkable, and clear of every other volume.
        auto Height=[&](const FERRoomsLink& E){return FInt32Interval(FMath::FloorToInt32(FMath::Min(E.Start.Z,E.End.Z)),FMath::CeilToInt32(FMath::Max(E.Start.Z,E.End.Z)+H));};
        auto Touches=[](const FInt32Interval& A,int32 Low,int32 High){return A.Min<High+20 && Low<A.Max+20;};
        for(int32 I=FirstLink;I<L.RoomLinks.Num();++I)
        {
            const auto& E=L.RoomLinks[I];const FVector Start(E.Start.X,E.Start.Y,0),End(E.End.X,E.End.Y,0);
            const float Run=FVector::Dist2D(Start,End);
            if(Run<1 || FMath::Abs(E.End.Z-E.Start.Z)>Run*.5f)return Fail();
            const auto Span=Height(E);
            for(int32 Index=0;Index<L.RoomNodes.Num();++Index)if(Index!=E.A && Index!=E.B)
            {
                const auto& V=L.RoomNodes[Index];
                if(!Touches(Span,FMath::FloorToInt32(V.Center.Z),FMath::CeilToInt32(V.Center.Z+V.Height)))continue;
                const FVector Near=FMath::ClosestPointOnSegment(FVector(V.Center.X,V.Center.Y,0),Start,End);
                if(FMath::Abs(Near.X-V.Center.X)<V.Size.X*.5f+E.Width*.5f+60 && FMath::Abs(Near.Y-V.Center.Y)<V.Size.Y*.5f+E.Width*.5f+60)return Fail();
            }
            for(int32 J=0;J<L.RoomLinks.Num();++J)if(J!=I)
            {
                const auto& F=L.RoomLinks[J];const auto Other=Height(F);
                if(F.A==E.A || F.A==E.B || F.B==E.A || F.B==E.B || !Touches(Span,Other.Min,Other.Max))continue;
                FVector P,Q;FMath::SegmentDistToSegmentSafe(Start,End,FVector(F.Start.X,F.Start.Y,0),FVector(F.End.X,F.End.Y,0),P,Q);
                if(FVector::Dist2D(P,Q)<E.Width+60)return Fail();
            }
        }
        L.RoomsMacroVariant=Variant.BakeSeed+1000*N;
        MacroSelfLinks=L.RoomLinks.Num()-Pairs;
        return true;
    };
    bool Macro=false;
    // A plan may lose what this seed has no room for: a steep rung, or the bend between close bases.
    if(C.bEnablePCGExMacroGraph && N>1)for(const auto* Variant:ERRoomsMacro::Candidates(R.MacroTable,N,C.Seed))
    {
        Macro=ApplyMacro(*Variant,true,true) || ApplyMacro(*Variant,true,false) || ApplyMacro(*Variant,false,true);
        if(Macro)break;
    }
    if(N>1 && !Macro)Segment(SpineA,SpineB);
    if(!GroundOnly)Segment(UpperLeft,UpperRight,N%2 ? 620:0,N%2 ? 620:0);
    if(N==1 && HallPassage)
    {
        const auto& Hall=L.RoomNodes[2];
        const bool Horizontal=Hall.Center.Y+Hall.Size.Y*.5f<0;
        const FVector Mouth=Horizontal ? FVector(0,Hall.Center.Y+Hall.Size.Y*.5f,0):FVector(Hall.Center.X-Hall.Size.X*.5f,0,0);
        L.RoomLinks.Add({2,3,Mouth,Turn(Mouth),R.CorridorWidth,H,R.CornerCut});
    }
    if(T->SizingTeamSize>0)for(const auto& E:L.RoomLinks)
        if(FVector::Dist2D(E.Start,E.End)<1 || FMath::Abs(E.End.Z-E.Start.Z)>FVector::Dist2D(E.Start,E.End)*.5f+.01f)return false;
    const int32 SelfLinks=N==1 ? (HallPassage ? 1:0):(Macro ? MacroSelfLinks:1)+(GroundOnly ? 0:1);
    TMap<int32,TArray<FVector>> GalleryPaths;
    auto Face=[&](FVector A,FVector D,FVector E,FVector F,UMaterialInterface* Surface,FVector Normal,bool Roof=false)
    {
        if(FVector::DotProduct(FVector::CrossProduct(D-A,E-A),Normal)<0)
        {
            if(E.Equals(F,.001)){Swap(D,E);F=E;}else Swap(D,F);
        }
        L.RoomShell.Add({A,D,E,F,Surface,Roof});
    };
    auto Slab=[&](const FERRoomsNode& V,float Z,bool Roof)
    {
        const FVector Center=V.Center+FVector(0,0,Z);
        for(int32 I=0;I<V.Polygon.Num();++I)
        {
            const FVector A=Center+FVector(V.Polygon[I],0),D=Center+FVector(V.Polygon[(I+1)%V.Polygon.Num()],0);
            Face(Center,A,D,D,Roof ? Theme.Ceiling.Get():Theme.Floor.Get(),Roof ? -FVector::UpVector:FVector::UpVector,Roof);
        }
    };
    auto Box=[&](FVector Center,FVector Size,UMaterialInterface* Surface,FRotator Rotation=FRotator::ZeroRotator,bool Visual=false)
    {L.FixedPieces.Add({FTransform(Rotation,Center,Size/100),EERBlockMaterial::Concrete,nullptr,FRotator::ZeroRotator,false,Visual,Surface});};
    auto Ramp=[&](FVector A,FVector D,float Width)
    {
        const FVector Side=FVector::CrossProduct(FVector::UpVector,(D-A).GetSafeNormal2D())*Width*.5;
        Face(A-Side,D-Side,D+Side,A+Side,Theme.Floor,FVector::UpVector);
    };
    for(int32 Index=0;Index<L.RoomNodes.Num();++Index)
    {
        const auto& V=L.RoomNodes[Index];Slab(V,0,false);Slab(V,V.Height,true);
        if(!V.Junction)L.Rooms.Add({FVector2D(V.Center)-V.Size*.5,FVector2D(V.Center)+V.Size*.5,Index,float(V.Center.Z),0});
        if(V.Junction)L.RoomLightPoints.Add(V.Center+FVector(0,0,H-60));
        else for(float SX:{-1.f,1.f})for(float SY:{-1.f,1.f})
        {
            const FVector XY(SX*V.Size.X*.22f,SY*V.Size.Y*.22f,0);
            L.RoomLightPoints.Add(V.Center+XY+FVector(0,0,V.Upper ? 250:H-60));
            if(V.Upper)L.RoomLightPoints.Add(V.Center+XY+FVector(0,0,620));
            if(R.LampSurface)Box(V.Center+XY+FVector(0,0,V.Height-12),{90,18,8},R.LampSurface,FRotator::ZeroRotator,true);
        }
        for(int32 I=0;I<V.Polygon.Num();++I)
        {
            const FVector A=V.Center+FVector(V.Polygon[I],0),D=V.Center+FVector(V.Polygon[(I+1)%V.Polygon.Num()],0),Along=(D-A).GetSafeNormal();
            const FVector Normal=FVector::CrossProduct(FVector::UpVector,Along);const float Length=(D-A).Size();
            TArray<FVector> Portals;
            for(const auto& E:L.RoomLinks)if(E.A==Index || E.B==Index)
            {
                const FVector P=E.A==Index ? E.Start:E.End;
                const FVector Ground(P.X,P.Y,V.Center.Z);
                if(FVector::DistSquared(Ground,FMath::ClosestPointOnSegment(Ground,A,D))<.01){Portals.Add(P);}
            }
            auto Wall=[&](float From,float To,float Bottom,float Top)
            {if(To-From>.1f && Top-Bottom>.1f)Face(A+Along*From+FVector(0,0,Bottom),A+Along*To+FVector(0,0,Bottom),A+Along*To+FVector(0,0,Top),A+Along*From+FVector(0,0,Top),Theme.Wall,Normal);};
            if(!Portals.IsEmpty())
            {
                const float At=FVector::DotProduct(Portals[0]-A,Along);
                const bool SeparateMouths=Portals.ContainsByPredicate([&](FVector P){return FMath::Abs(FVector::DotProduct(P-A,Along)-At)>1;});
                if(SeparateMouths)
                {
                    TArray<float> Cuts{0,Length},Levels{0,V.Height};
                    for(const auto P:Portals)
                    {
                        const float X0=FVector::DotProduct(P-A,Along),Z0=P.Z-V.Center.Z;
                        Cuts.AddUnique(FMath::Clamp(X0-R.CorridorWidth*.5f,0.f,Length));Cuts.AddUnique(FMath::Clamp(X0+R.CorridorWidth*.5f,0.f,Length));
                        Levels.AddUnique(FMath::Clamp(Z0,0.f,V.Height));Levels.AddUnique(FMath::Clamp(Z0+H,0.f,V.Height));
                    }
                    Cuts.Sort();Levels.Sort();
                    for(int32 X0=1;X0<Cuts.Num();++X0)for(int32 Z0=1;Z0<Levels.Num();++Z0)
                    {
                        const float PX=(Cuts[X0-1]+Cuts[X0])*.5f,PZ=(Levels[Z0-1]+Levels[Z0])*.5f;
                        const bool Open=Portals.ContainsByPredicate([&](FVector P){return FMath::Abs(PX-FVector::DotProduct(P-A,Along))<R.CorridorWidth*.5f && PZ>P.Z-V.Center.Z && PZ<P.Z-V.Center.Z+H;});
                        if(!Open)Wall(Cuts[X0-1],Cuts[X0],Levels[Z0-1],Levels[Z0]);
                    }
                }
                else
                {
                    Wall(0,At-R.CorridorWidth*.5f,0,V.Height);Wall(At+R.CorridorWidth*.5f,Length,0,V.Height);
                    TArray<float> Levels{0,V.Height};for(const auto P:Portals){Levels.Add(P.Z-V.Center.Z);Levels.Add(P.Z-V.Center.Z+H);}Levels.Sort();
                    for(int32 J=1;J<Levels.Num();++J)
                    {
                        const float Mid=(Levels[J-1]+Levels[J])*.5f;
                        if(!Portals.ContainsByPredicate([&](const auto P){return Mid>P.Z-V.Center.Z && Mid<P.Z-V.Center.Z+H;}))
                            Wall(At-R.CorridorWidth*.5f,At+R.CorridorWidth*.5f,Levels[J-1],Levels[J]);
                    }
                }
                // Match the octagonal mouth exactly. A rectangular opening
                // would expose four triangular gaps outside the corridor shell.
                for(const auto Portal:Portals)for(float Side:{-1.f,1.f})for(float Top:{0.f,1.f})
                {
                    const FVector Corner=Portal+FVector(0,0,Top*H)+Along*(Side*R.CorridorWidth*.5f);
                    const FVector B0=Corner-Along*(Side*R.CornerCut),C0=Corner+FVector(0,0,(Top ? -1:1)*R.CornerCut);
                    // The corridor prism also has an end cap at this plane.
                    // A 2mm reveal keeps the visible infill clear of that cap.
                    const FVector Reveal=Normal*.2f;
                    Face(Corner+Reveal,B0+Reveal,C0+Reveal,C0+Reveal,Theme.Wall,Normal);
                }
            }
            else Wall(0,Length,0,V.Height);
        }
        if(V.Upper)
        {
            // A switchback of two smooth 310cm rises beside the unused outer
            // wall. Ground doors and the central crossing retain full headroom.
            const float Sign=Index<2+N ? 1.f:-1.f,A=V.Size.X*.5f-300;
            auto P=[&](float PX,float PY,float Z){return V.Center+FVector(PX*Sign,PY*Sign,Z);};
            const float Y1=-V.Size.Y*.5f+160,Y2=Y1+200;
            Ramp(P(-A,Y1,0),P(A,Y1,310),160);Ramp(P(A,Y2,310),P(-A,Y2,620),160);
            Box(P(A+80,(Y1+Y2)*.5f,300),{160,360,20},Theme.Floor);
            const float MinX=-V.Size.X*.5f+40,MaxX=-A,MinY=Y2-95,MaxY=V.Size.Y*.5f-180;
            Box(P((MinX+MaxX)*.5f,(MinY+MaxY)*.5f,610),{MaxX-MinX,MaxY-MinY,20},Theme.Floor);
            // A short solid rail protects the gallery while leaving its entry open.
            Box(P(MaxX-5,(Y2+100+MaxY)*.5f,670),{10,MaxY-Y2-100,100},Theme.Wood);
            TArray<FVector> Route{P(-A-100,Y1,100),P(-A,Y1,100),P(A,Y1,410),P(A+80,Y1,410),P(A+80,Y2,410),P(A,Y2,410),P(-A,Y2,720),P(-A-60,Y2,720),P(-A-60,MaxY-80,720)};
            GalleryPaths.Add(Index,Route);L.StairRoutes.Add(MoveTemp(Route));
            // Continue the landing around the north and inner walls. Upper
            // corridor mouths now open onto a supported gallery, not empty air.
            Box(P(0,V.Size.Y*.5f-120,610),{V.Size.X-80,240,20},Theme.Floor);
            Box(P(V.Size.X*.5f-120,(Y2+200+V.Size.Y*.5f-40)*.5f,610),{240,V.Size.Y*.5f-40-Y2-200,20},Theme.Floor);
            FBox Deck(ForceInit);Deck+=P(MinX,MinY,600);Deck+=P(MaxX,MaxY,620);L.ElevatedDecks.Add(Deck);
        }
    }
    for(const auto& E:L.RoomLinks)
    {
        const FVector Dir=(E.End-E.Start).GetSafeNormal2D(),Side=FVector::CrossProduct(FVector::UpVector,Dir);
        const float W=E.Width*.5f,K=E.Cut;
        // The octagon is a rectangular section with four clipped corners.
        // Elevation is sheared along the run, preserving vertical clearance.
        const FVector2D Section[]={{-W+K,0},{W-K,0},{W,K},{W,H-K},{W-K,H},{-W+K,H},{-W,H-K},{-W,K}};
        for(int32 I=0;I<8;++I)
        {
            const auto A=Section[I],D=Section[(I+1)%8];
            const FVector P=Side*A.X+FVector(0,0,A.Y),Q=Side*D.X+FVector(0,0,D.Y);
            const FVector Inward=-Side*((A.X+D.X)*.5f)+FVector(0,0,H*.5f-(A.Y+D.Y)*.5f);
            const bool Roof=I>=3 && I<=5;
            Face(E.Start+P,E.End+P,E.End+Q,E.Start+Q,I==0 ? Theme.Floor.Get():Roof ? Theme.Ceiling.Get():Theme.Wall.Get(),Inward,Roof);
        }
        const int32 Lamps=FMath::Max(1,FMath::CeilToInt(FVector::Dist2D(E.Start,E.End)/600));
        for(int32 I=0;I<Lamps;++I)
        {
            const FVector P=FMath::Lerp(E.Start,E.End,(I+.5f)/Lamps);
            L.RoomLightPoints.Add(P+FVector(0,0,H-65));
            if(R.LampSurface)Box(P+FVector(0,0,H-12),{80,18,8},R.LampSurface,Dir.Rotation(),true);
        }
    }
    int32 CoverId=0;
    auto AddScreen=[&](FVector Center,float Yaw,FVector Size,int32 Link,int32 Room)
    {
        const int32 First=L.Panels.Num();const FTransform Frame(FRotator(0,Yaw,0),Center);
        AddPieceCells(CoverId++,EERBlockMaterial::Wood,FBox(-Size*.5,Size*.5),true,Cells,L.Panels);
        for(int32 I=First;I<L.Panels.Num();++I){L.Panels[I].Transform=L.Panels[I].Transform*Frame;L.Panels[I].Surface=Theme.Wood;}
        L.LowCovers.Add(FBox(-Size*.5,Size*.5).TransformBy(Frame));
        L.RoomScreens.Add({Frame,Size,Link,Room});
    };
    if(T->SizingTeamSize>0)
    {
        for(int32 BaseIndex:{AlphaBase,OmegaBase})
        {
            const auto& Base=L.RoomNodes[BaseIndex];const float Sign=BaseIndex==AlphaBase ? 1.f:-1.f;
            auto At=[&](float PX,float PY){return Base.Center+FVector(Sign*PX,Sign*PY,150);};
            const float Yaw=Sign>0 ? 0:180;
            if(N==1)
            {
                // Corner bases have two perpendicular mouths. An L-shaped
                // timber baffle screens both without sealing either exit.
                AddScreen(At(50,0),Yaw+90,{600,Cells.WoodDepth,300},INDEX_NONE,BaseIndex);
                AddScreen(At(-150.5f,300),Yaw,{399,Cells.WoodDepth,300},INDEX_NONE,BaseIndex);
            }
            else
            {
                const float Width=FMath::Min(1000.f,float(Base.Size.X)-400);
                AddScreen(At(0,100),Yaw,{Width,Cells.WoodDepth,300},INDEX_NONE,BaseIndex);
            }
        }
    }
    // Long runs alone get screens at five-metre intervals. Self-symmetric
    // links use an even count so reverse order and side remain half-turn paired.
    L.RoomAccessRoutes.SetNum(L.RoomLinks.Num());
    auto FurnishLink=[&](int32 Index,int32 Count,float FirstGap)
    {
        const auto& E=L.RoomLinks[Index];const float Length=FVector::Dist2D(E.Start,E.End);
        const FVector Dir=(E.End-E.Start).GetSafeNormal2D(),Side=FVector::CrossProduct(FVector::UpVector,Dir);
        const float Offset=E.Width*.5f-E.Cut-20,Margin=FMath::Min(60.f,FMath::Max(42.f,Length*.12f));
        const float Slope=FMath::Abs(E.End.Z-E.Start.Z)/Length;
        auto Point=[&](float Along,float Across,float Z=100){return FMath::Lerp(E.Start,E.End,Along/Length)+Side*Across+FVector(0,0,Z);};
        auto Entry=[&](int32 NodeIndex,FVector Portal)
        {
            const auto& V=L.RoomNodes[NodeIndex];TArray<FVector> Route{V.Center+FVector(0,0,100)};
            if(T->SizingTeamSize>0 && V.Base)
            {
                const float Sign=NodeIndex==AlphaBase ? 1.f:-1.f;
                const FVector Local=(Portal-V.Center)*Sign;
                auto At=[&](float PX,float PY){return V.Center+FVector(Sign*PX,Sign*PY,100);};
                if(N==1)
                {
                    if(Local.Y>300)
                    {Route.Add(At(-390,0));Route.Add(At(-390,350));Route.Add(At(Local.X,350));}
                    else
                    {Route.Add(At(0,-350));Route.Add(At(150,-350));Route.Add(At(150,0));}
                }
                else if(Local.Y>100)
                {
                    const float Width=FMath::Min(1000.f,float(V.Size.X)-400);
                    const float Bypass=FMath::Min(Width*.5f+120,float(V.Size.X)*.5f-100);
                    Route.Add(At(Bypass,0));Route.Add(At(Bypass,200));Route.Add(At(Local.X,200));
                }
            }
            else if(Portal.Z>V.Center.Z+200 && GalleryPaths.Contains(NodeIndex))
            {
                Route.Append(GalleryPaths[NodeIndex]);const float Sign=NodeIndex<2+N ? 1.f:-1.f;
                auto P=[&](float X0,float Y0){return V.Center+FVector(X0*Sign,Y0*Sign,720);};
                const FVector Local=(Portal-V.Center)*Sign;
                Route.Add(P(-V.Size.X*.5f+140,V.Size.Y*.5f-140));
                Route.Add(P(Local.X>V.Size.X*.4f ? V.Size.X*.5f-140:Local.X,V.Size.Y*.5f-140));
                if(Local.X>V.Size.X*.4f)Route.Add(P(V.Size.X*.5f-140,Local.Y));
            }
            else if(V.Upper)
            {
                const float Sign=NodeIndex<2+N ? 1.f:-1.f;const FVector Local=(Portal-V.Center)*Sign;
                if(Local.Y<-V.Size.Y*.4f)
                {
                    auto P=[&](float X0,float Y0){return V.Center+FVector(X0*Sign,Y0*Sign,100);};
                    Route.Add(P(-V.Size.X*.5f+180,0));Route.Add(P(-V.Size.X*.5f+180,-V.Size.Y*.5f+40));Route.Add(P(0,-V.Size.Y*.5f+40));
                }
            }
            Route.Add(Portal+FVector(0,0,100));return Route;
        };
        auto& Path=L.RoomAccessRoutes[Index];Path=Entry(E.A,E.Start);
        for(int32 I=0;I<Count;++I)
        {
            const float Along=(Length-(Count-1)*500)*.5f+I*500,Gap=(I%2 ? -FirstGap:FirstGap);
            const float A=Gap>0 ? -E.Width*.5f:-10,B=Gap>0 ? 10:E.Width*.5f;
            const float Height=240+Slope*Cells.WoodDepth;
            const FVector Center=Point(Along,(A+B)*.5f,120);
            AddScreen(Center,Side.Rotation().Yaw+(Index>=L.RoomLinks.Num()-SelfLinks && I>=Count/2 ? 180:0),{B-A,Cells.WoodDepth,Height},Index,INDEX_NONE);
            Path.Add(Point(Along-Margin,Gap*Offset));Path.Add(Point(Along+Margin,Gap*Offset));
        }
        auto Exit=Entry(E.B,E.End);Algo::Reverse(Exit);Path.Append(Exit);
    };
    auto ScreenCount=[&](int32 I){const float Length=FVector::Dist2D(L.RoomLinks[I].Start,L.RoomLinks[I].End);return Length>=1000 ? FMath::FloorToInt(Length/500):0;};
    // All generated pairs precede the lower spine and the upper self-link.
    for(int32 I=0;I<L.RoomLinks.Num()-SelfLinks;I+=2)
    {
        const float Gap=(HashCombineFast(uint32(C.Seed),uint32(I))&1) ? 1.f:-1.f;
        FurnishLink(I,ScreenCount(I),Gap);FurnishLink(I+1,ScreenCount(I+1),Gap);
    }
    for(int32 I=L.RoomLinks.Num()-SelfLinks;I<L.RoomLinks.Num();++I)
        FurnishLink(I,(ScreenCount(I)/2)*2,(uint32(C.Seed)&1) ? 1.f:-1.f);
    TArray<TArray<FVector>> SpawnRoutes;
    // Reserve every base start and its way to the room centre before boards.
    for(int32 Row=0;Row<2;++Row)for(int32 Col=0;Col<4;++Col)
    {
        const auto& Base=L.RoomNodes[AlphaBase];
        const FVector P=N==1 ? Base.Center+FVector(-Base.Size.X*.5f+200+Row*160,(Col-1.5f)*140,100):FVector((Col-1.5f)*140,-Y-S*.5f+200+Row*160,100);
        L.AlphaSpawns.Emplace(FRotator(0,N==1 ? 45:90,0),P);L.OmegaSpawns.Emplace(FRotator(0,N==1 ? 225:270,0),Turn(P));
        SpawnRoutes.Add({P,Base.Center+FVector(0,0,100)});SpawnRoutes.Add({Turn(P),L.RoomNodes[OmegaBase].Center+FVector(0,0,100)});
    }
    // Sparse independent boards, with at least 5m between their centres.
    // Reject a pocket if it would intersect a portal or gallery approach.
    auto FurnishRoom=[&](int32 Index)
    {
        const auto& V=L.RoomNodes[Index];const int32 Paired=Mirror(Index);
        for(float SX:{-1.f,1.f})
        {
            const FVector P=V.Center+FVector(SX*V.Size.X*.26f,V.Size.Y*.23f,120);
            const FVector Size(FMath::Min(float(V.Size.X)*.25f,450.f),Cells.WoodDepth,240);
            const FTransform Frame(P);bool Clear=true;
            for(const auto* Routes:{&L.RoomAccessRoutes,&L.StairRoutes,&SpawnRoutes})for(const auto& Route:*Routes)
                for(int32 I=1;I<Route.Num();++I)
                    if(FMath::LineBoxIntersection(FBox(-Size*.5,Size*.5).ExpandBy(FVector(40,40,95)),Frame.InverseTransformPosition(Route[I-1]),Frame.InverseTransformPosition(Route[I]),Frame.InverseTransformVector(Route[I]-Route[I-1])))Clear=false;
            const auto Standing=FBox(-Size*.5,Size*.5).ExpandBy(FVector(40,40,95));
            for(const auto* Starts:{&L.AlphaSpawns,&L.OmegaSpawns})for(const auto& Start:*Starts)
                if(Standing.IsInsideOrOn(Frame.InverseTransformPosition(Start.GetLocation())))Clear=false;
            if(Clear){AddScreen(P,0,Size,INDEX_NONE,Index);AddScreen(Turn(P),180,Size,INDEX_NONE,Paired);}
        }
    };
    FurnishRoom(0);for(int32 Row=0;Row<N;++Row)FurnishRoom(2+Row);
    auto LaneRoute=[&](bool Mirrored)
    {
        TArray<FVector> Path;TArray<int32> Previous,Via;Previous.Init(INDEX_NONE,L.RoomNodes.Num());Via=Previous;
        const int32 Start=Mirrored ? OmegaBase:AlphaBase,Goal=Mirrored ? AlphaBase:OmegaBase;TArray<int32> Queue{Start};Previous[Start]=Start;
        for(int32 Q=0;Q<Queue.Num() && Previous[Goal]==INDEX_NONE;++Q)for(int32 I=0;I<L.RoomLinks.Num()-SelfLinks;++I)
        {
            const auto& E=L.RoomLinks[I];const int32 Next=E.A==Queue[Q] ? E.B:E.B==Queue[Q] ? E.A:INDEX_NONE;
            if(Next!=INDEX_NONE && Previous[Next]==INDEX_NONE){Previous[Next]=Queue[Q];Via[Next]=I;Queue.Add(Next);}
        }
        TArray<int32> Steps;for(int32 At=Goal;At!=Start;At=Previous[At]){if(At==INDEX_NONE)return Path;Steps.Add(At);}Algo::Reverse(Steps);
        for(int32 At:Steps)
        {
            auto Access=L.RoomAccessRoutes[Via[At]];if(L.RoomLinks[Via[At]].A==At)Algo::Reverse(Access);
            for(int32 J=Path.IsEmpty() ? 0:1;J<Access.Num();++J)Path.Add(Access[J]);
        }
        return Path;
    };
    L.CrossingRoutes.Add(LaneRoute(false));L.CrossingRoutes.Add(LaneRoute(true));
    TArray<FVector> Patrol=L.CrossingRoutes[0];const auto& Other=L.CrossingRoutes[1];
    for(int32 I=1;I<Other.Num()-1;++I)Patrol.Add(Other[I]);
    L.Spawn=L.AlphaSpawns[0].GetLocation();
    L.Routes={{L.Spawn,L.RoomNodes[AlphaBase].Center+FVector(0,0,100)},Patrol,{L.OmegaSpawns[0].GetLocation(),L.RoomNodes[OmegaBase].Center+FVector(0,0,100)}};
    if(N==1)L.Routes.Append(SpawnRoutes);
    if(N==1 && HallPassage)L.Routes.Add(L.RoomAccessRoutes.Last());
    L.ArenaSize={2*(X+S*.5f+R.PlanMargin),2*Y+S+400,1700};
    for(const auto& RoomNode:L.RoomNodes)
        if(FMath::Abs(RoomNode.Center.X)+RoomNode.Size.X*.5+20>L.ArenaSize.X*.5 || FMath::Abs(RoomNode.Center.Y)+RoomNode.Size.Y*.5+20>L.ArenaSize.Y*.5)return false;
    // This sealed complex uses ground-level player sight; aerial recon remains
    // above the roof. No warehouse orbit may pass through these separate rooms.
    L.FeatureNames={Theme.Name,TEXT("OctagonalCorridors"),TEXT("SwitchbackRamps"),TEXT("TimberScreens"),TEXT("SpacedTimberScreens"),TEXT("GalleryConnections"),TEXT("OverUnderCrossing"),TEXT("DoglegCorridors")};
    if(Macro)L.FeatureNames.Add(TEXT("PCGExMacroGraph"));
    if(N==1)
    {
        if(HallPassage)L.FeatureNames.Add(TEXT("HallPassage"));
        L.FeatureNames.Add(HallStyle==2 ? TEXT("IrregularHalls"):HallStyle==1 ? TEXT("CompactHalls"):TEXT("RectangularHalls"));
    }
    // Validate whole swept standing routes against every oriented screen before
    // publishing; all paths follow the real sloped supporting floor.
    for(const auto* Paths:{&L.RoomAccessRoutes,&L.StairRoutes,&L.CrossingRoutes,&SpawnRoutes})for(const auto& Path:*Paths)
        for(int32 I=1;I<Path.Num();++I)for(const auto& Screen:L.RoomScreens)
        {
            const FBox Bounds=FBox(-Screen.Size*.5,Screen.Size*.5).ExpandBy(FVector(35,35,93));
            const FVector A=Screen.Frame.InverseTransformPosition(Path[I-1]),D=Screen.Frame.InverseTransformPosition(Path[I]);
            if(FMath::LineBoxIntersection(Bounds,A,D,D-A))
            {UE_LOG(LogTemp,Warning,TEXT("ER_ROOMS_SCREEN_ROUTE_REJECT seed=%d link=%d room=%d from=%s to=%s"),C.Seed,Screen.Link,Screen.Room,*Path[I-1].ToString(),*Path[I].ToString());return false;}
        }
    Out=MoveTemp(L);return true;
}

void BuildRoomsShell(AERProceduralMap* Map,const FERProceduralLayout& L,TArray<TObjectPtr<UDynamicMeshComponent>>& Components)
{
    using namespace UE::Geometry;
    struct FGroup { UMaterialInterface* Surface; bool Roof; FDynamicMesh3 Mesh; };
    TArray<FGroup> Groups;
    for(const auto& F:L.RoomShell)
    {
        int32 G=Groups.IndexOfByPredicate([&](const FGroup& V){return V.Surface==F.Surface && V.Roof==F.Roof;});
        if(G==INDEX_NONE){G=Groups.Add({F.Surface,F.Roof,FDynamicMesh3()});Groups[G].Mesh.EnableAttributes();}
        auto& Mesh=Groups[G].Mesh;auto* Normals=Mesh.Attributes()->PrimaryNormals();auto* UVs=Mesh.Attributes()->PrimaryUV();
        TArray<FVector> Front{F.A,F.B,F.C};if(!F.D.Equals(F.C,.001) && !F.D.Equals(F.A,.001))Front.Add(F.D);
        const FVector N=FVector::CrossProduct(Front[1]-Front[0],Front[2]-Front[0]).GetSafeNormal();
        TArray<FVector> Back;for(const auto P:Front)Back.Add(P-N*20);
        auto Polygon=[&](const TArray<FVector>& Points,FVector Normal)
        {
            TArray<int32> V,NE,UV;const FVector U=(Points[1]-Points[0]).GetSafeNormal(),D=FVector::CrossProduct(Normal,U);
            for(const auto P:Points){V.Add(Mesh.AppendVertex(P));NE.Add(Normals->AppendElement(FVector3f(Normal)));UV.Add(UVs->AppendElement(FVector2f(FVector::DotProduct(P,U),FVector::DotProduct(P,D))/100));}
            for(int32 I=1;I<Points.Num()-1;++I)
            {
                // Unreal's visible winding is edge2 x edge1.
                const int32 Tri=Mesh.AppendTriangle(V[0],V[I+1],V[I]);
                if(Tri>=0){Normals->SetTriangle(Tri,FIndex3i(NE[0],NE[I+1],NE[I]));UVs->SetTriangle(Tri,FIndex3i(UV[0],UV[I+1],UV[I]));}
            }
        };
        Polygon(Front,N);Algo::Reverse(Back);Polygon(Back,-N);Algo::Reverse(Back);
        for(int32 I=0;I<Front.Num();++I)
        {
            const int32 J=(I+1)%Front.Num();const TArray<FVector> Side{Front[J],Front[I],Back[I],Back[J]};
            Polygon(Side,FVector::CrossProduct(Side[1]-Side[0],Side[2]-Side[0]).GetSafeNormal());
        }
    }
    for(auto& G:Groups)
    {
        auto* Part=NewObject<UDynamicMeshComponent>(Map,*FString::Printf(TEXT("RoomsShell_%d"),Components.Num()));
        Map->AddInstanceComponent(Part);Part->SetMobility(EComponentMobility::Movable);Part->SetNetAddressable();
        Part->SetupAttachment(Map->GetRootComponent());Part->SetCanEverAffectNavigation(false);
        Part->SetDeferredCollisionUpdatesEnabled(true,false);Part->SetComplexAsSimpleCollisionEnabled(true,false);
        Part->SetCollisionProfileName(TEXT("BlockAll"));Part->SetTangentsType(EDynamicMeshComponentTangentsMode::AutoCalculated);
        Part->SetMaterial(0,G.Surface);if(G.Roof)Part->ComponentTags.Add(TEXT("ERTemplateRoof"));
        Part->RegisterComponent();Part->SetMesh(MoveTemp(G.Mesh));Part->UpdateCollision(false);Components.Add(Part);
    }
}
}
