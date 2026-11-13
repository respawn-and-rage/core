#include "ERProceduralMap.h"

namespace ERProcedural
{
void PlanVillageWatercourses(FERProceduralLayout& L,int32 Seed,bool Mirror)
{
    FRandomStream WaterRng(int32(HashCombine(uint32(Seed),0x56494c57u)));
    if(WaterRng.RandRange(0,3)==0)return;

    const double HX=L.ArenaSize.X*.5,HY=L.ArenaSize.Y*.5;
    const double Bank=WaterRng.RandRange(95,110),Depth=WaterRng.RandRange(50,60);
    constexpr double Lip=10,Run=100,Rise=22,DeckOverlap=20,BridgeHalfWidth=85;
    const double DeckEnd=Bank+DeckOverlap,Approach=DeckEnd+Run;
    TArray<FBox> Occupied;
    auto Reserve=[&](const FBox& Bounds)
    {
        // The perimeter continues across the mouths of a transverse stream.
        if(Bounds.Max.Z>=0 && Bounds.Min.Z<260 && FMath::Abs(Bounds.GetCenter().X)<HX &&
            FMath::Abs(Bounds.GetCenter().Y)<HY)Occupied.Add(Bounds);
    };
    for(const auto& P:L.Panels)Reserve(FBox(FVector::ZeroVector,P.Size).TransformBy(P.Transform));
    for(const auto& P:L.FixedPieces)if(!P.VisualOnly && !P.PawnBarrier)
        Reserve(FBox(FVector(-50),FVector(50)).TransformBy(P.Transform));
    Occupied.Append(L.TownBuildings);
    const auto Pair=[&](const FBox2D& Box)
    {
        return FBox2D({Mirror ? Box.Min.X:-Box.Max.X,-Box.Max.Y},
                      {Mirror ? Box.Max.X:-Box.Min.X,-Box.Min.Y});
    };
    auto Clear=[&](const FBox2D& Footprint)
    {
        for(const auto& Bounds:Occupied)
        {
            const FBox2D Margin(FVector2D(Bounds.Min)-FVector2D(45),FVector2D(Bounds.Max)+FVector2D(45));
            if(Footprint.Intersect(Margin))return false;
        }
        return true;
    };
    auto Flat=[&](const FBox2D& Footprint)
    {
        const double H=VillageGroundHeight(L,Footprint.Min);
        if(!FMath::IsNearlyEqual(H,double(VillageGroundHeight(L,Footprint.Max)),.01))return false;
        for(const auto& Knot:L.VillageTerrainProfile)
            if(Knot.X>Footprint.Min.Y && Knot.X<Footprint.Max.Y && !FMath::IsNearlyEqual(Knot.Y,H,.01))return false;
        return true;
    };
    auto Water=[&](const FBox2D& Box)
    {
        const double Surface=VillageGroundHeight(L,Box.GetCenter())-Lip;
        L.PoolWater.Emplace(FVector(Box.Min,Surface-Depth),FVector(Box.Max,Surface));
    };
    auto Bridge=[&](FVector2D Center,bool AlongX)
    {
        // The half-turn layout is fitted before terrain offsets are applied.
        const double Ground=Mirror ? VillageGroundHeight(L,Center):0;
        const FVector Axis=AlongX ? FVector::XAxisVector:FVector::YAxisVector;
        const FQuat Yaw(FVector::UpVector,AlongX ? -UE_HALF_PI:0.);
        auto Point=[&](double Offset,double Height)
        {
            return FVector(Center,Ground+Height)+Axis*Offset;
        };
        auto Timber=[&](FVector A,FVector B,double Thickness)
        {
            const FVector Along=B-A;
            const FQuat Rotation=Yaw*FQuat(FVector::XAxisVector,FMath::Atan2(Along.Z,FVector::DotProduct(Along,Axis)));
            FERLayoutFixedPiece Piece;
            Piece.Material=EERBlockMaterial::Wood;
            Piece.Transform=FTransform(Rotation,(A+B)*.5-Rotation.RotateVector({0,0,Thickness*.5}),
                FVector(BridgeHalfWidth*2,Along.Size(),Thickness)/100);
            L.FixedPieces.Add(MoveTemp(Piece));
        };
        Timber(Point(-DeckEnd,Rise),Point(DeckEnd,Rise),16);
        Timber(Point(-Approach,0),Point(-DeckEnd,Rise),12);
        Timber(Point(DeckEnd,Rise),Point(Approach,0),12);
        L.GroundRoutes.Add({Point(-Approach,100),Point(-DeckEnd,100+Rise),
            Point(DeckEnd,100+Rise),Point(Approach,100)});
    };
    // Keep existing routes intact unless they cross a transverse channel
    // squarely on one of the flank bridges. Leave room for a standing capsule.
    auto CheckPaths=[&](const FBox2D& Footprint,bool Transverse)
    {
        const FBox Margin(FVector(Footprint.Min,-10000)-FVector(45,45,0),
                          FVector(Footprint.Max,10000)+FVector(45,45,0));
        auto ClearPath=[&](const FVector& A,const FVector& B,bool CanBridge)
        {
            if(!FMath::LineBoxIntersection(Margin,A,B,B-A))return true;
            return CanBridge && FMath::IsNearlyEqual(A.X,B.X,.01) &&
                FMath::Abs(FMath::Abs(A.X)-(HX-90))<=BridgeHalfWidth-40 &&
                FMath::Min(A.Y,B.Y)<Margin.Min.Y && FMath::Max(A.Y,B.Y)>Margin.Max.Y;
        };
        for(const auto* Paths:{&L.Routes,&L.StairRoutes,&L.VillageSideBypasses,&L.GroundRoutes,&L.SpawnExitRoutes})
            for(const auto& Path:*Paths)
                for(int32 I=1;I<Path.Num();++I)
                    if(!ClearPath(Path[I-1],Path[I],Transverse && Paths==&L.Routes))return false;
        for(const auto& Site:L.CoverSites)
            if(!ClearPath(Site.Entry,Site.Hide,false))return false;
        return true;
    };
    auto LiftPaths=[&](double Y)
    {
        for(auto* Paths:{&L.Routes,&L.CrossingRoutes})
            for(auto& Path:*Paths)
            {
                if(Path.IsEmpty())continue;
                TArray<FVector> Adjusted{Path[0]};
                for(int32 I=1;I<Path.Num();++I)
                {
                    const auto& A=Path[I-1];
                    const auto& B=Path[I];
                    if(FMath::Min(A.Y,B.Y)<Y-Approach && FMath::Max(A.Y,B.Y)>Y+Approach &&
                        FMath::IsNearlyEqual(A.X,B.X,.01) && FMath::Abs(FMath::Abs(A.X)-(HX-90))<=BridgeHalfWidth-40)
                    {
                        const double Sign=B.Y>A.Y ? 1:-1;
                        for(double Offset:{-Approach,-DeckEnd,DeckEnd,Approach})
                        {
                            const double At=Y+Offset*Sign;
                            auto Point=FMath::Lerp(A,B,(At-A.Y)/(B.Y-A.Y));
                            if(FMath::Abs(Offset)<=DeckEnd)Point.Z+=Rise;
                            Adjusted.Add(Point);
                        }
                    }
                    Adjusted.Add(B);
                }
                Path=MoveTemp(Adjusted);
            }
    };
    auto Transverse=[&]()
    {
        for(double Y=-HY+1550;Y<-Approach-60;Y+=50)
        {
            const FBox2D Envelope({-HX,Y-Approach-5},{HX,Y+Approach+5});
            if(!Flat(Envelope) || !Clear(Envelope) || !Clear(Pair(Envelope)) ||
                !CheckPaths(Envelope,true) || !CheckPaths(Pair(Envelope),true))continue;
            // Both channels continue under the palisades to a short outlet.
            // Keep a straight reach at each of the three bridge decks.
            constexpr double Outlet=150;
            const double End=HX+Outlet;
            const int32 HalfSamples=FMath::CeilToInt32(End/40);
            const double Amplitude=FMath::Min(110.,(HX-90)*.07);
            const double Phase=WaterRng.FRandRange(.25f,1.25f);
            auto Bend=[&](double X)
            {
                const double Distance=FMath::Min(FMath::Abs(X),FMath::Abs(FMath::Abs(X)-(HX-90)));
                const double Alpha=FMath::Clamp((Distance-125)/310,0.,1.);
                const double T=X/(HX-90);
                return X<-HX+90 || X>HX-90 ? 0. :
                    Amplitude*Alpha*Alpha*(3-2*Alpha)*FMath::Sin(T*2.7*UE_PI+Phase);
            };
            FERVillageStream First;
            First.Surface=VillageGroundHeight(L,{0,Y})-Lip;
            First.Depth=Depth;
            for(int32 I=-HalfSamples;I<=HalfSamples;++I)
            {
                const double X=End*I/HalfSamples;
                const double Slope=(Bend(X+1)-Bend(X-1))*.5;
                const FVector2D Normal=FVector2D(-Slope,1).GetSafeNormal();
                const FVector2D Center(X,Y+Bend(X));
                First.LowerBank.Add(Center-Normal*Bank);
                First.UpperBank.Add(Center+Normal*Bank);
            }
            FERVillageStream Second;
            Second.Surface=VillageGroundHeight(L,{0,-Y})-Lip;
            Second.Depth=Depth;
            for(int32 I=0;I<First.LowerBank.Num();++I)
            {
                const int32 J=Mirror ? I:First.LowerBank.Num()-1-I;
                const double Sign=Mirror ? 1.:-1.;
                Second.LowerBank.Emplace(Sign*First.UpperBank[J].X,-First.UpperBank[J].Y);
                Second.UpperBank.Emplace(Sign*First.LowerBank[J].X,-First.LowerBank[J].Y);
            }
            L.VillageStreams.Add(MoveTemp(First));
            L.VillageStreams.Add(MoveTemp(Second));
            if(!TriangulateVillageWaterways(L,0))
            {
                L.VillageStreams.Reset();
                continue;
            }
            for(const auto& Stream:L.VillageStreams)
            {
                for(int32 I=1;I<Stream.LowerBank.Num();++I)
                {
                    FBox2D Strip(ForceInit);
                    Strip+=Stream.LowerBank[I-1];Strip+=Stream.UpperBank[I-1];
                    Strip+=Stream.LowerBank[I];Strip+=Stream.UpperBank[I];
                    Water(Strip);
                }
            }
            for(double Row:{Y,-Y})
            {
                for(double X:{-HX+90,0.,HX-90})Bridge({X,Row},false);
                LiftPaths(Row);
                for(double X:{-HX-10,HX+10})
                {
                    // Continue the existing pawn-only palisade barrier down
                    // to the bed. Water and effects pass between the logs.
                    FERLayoutFixedPiece Barrier;
                    Barrier.Material=EERBlockMaterial::Wood;
                    Barrier.CollisionOnly=true;
                    Barrier.PawnBarrier=true;
                    const double Drop=Depth+Lip+5;
                    const double Ground=Mirror ? VillageGroundHeight(L,{X,Row}):0;
                    Barrier.Transform=FTransform(FQuat::Identity,{X,Row,Ground-Drop*.5+5},{.2,(Bank*2+20)/100,(Drop+10)/100});
                    L.FixedPieces.Add(MoveTemp(Barrier));
                }
            }
            L.FeatureNames.Add(TEXT("VillageTransverseStreams"));
            return true;
        }
        return false;
    };
    auto Loop=[&]()
    {
        // The dry interior must contain every central block. Fit the four
        // banks independently of the occupied land inside the ring.
        double CoreX=HX*.25,CoreY=HY*.25;
        for(const auto& Block:L.VillageBlocks)
        {
            const FVector Center=Block.Frame.GetLocation();
            CoreX=FMath::Max(CoreX,FMath::Abs(Center.X)+Block.Size.X*.5);
            CoreY=FMath::Max(CoreY,FMath::Abs(Center.Y)+Block.Size.Y*.5);
        }
        auto Reflect=[&](FVector2D P){return FVector2D(Mirror ? P.X:-P.X,-P.Y);};
        struct FCrossing { FVector2D Center; bool AlongX; };
        for(double Y=CoreY+Bank+60;Y<=HY-950;Y+=50)
        {
            if(!Flat(FBox2D({0,-Y-Bank},{0,Y+Bank})))break;
            for(double X=CoreX+Bank+60;X<=HX-Approach-5;X+=50)
            {
                const FBox2D Banks[]{
                    {{-X-Bank,-Y-Bank},{X+Bank,-Y+Bank}},
                    {{-X-Bank,Y-Bank},{X+Bank,Y+Bank}},
                    {{-X-Bank,-Y+Bank},{-X+Bank,Y-Bank}},
                    {{X-Bank,-Y+Bank},{X+Bank,Y-Bank}}
                };
                bool Fits=true;
                for(const auto& Channel:Banks)if(!Clear(Channel)){Fits=false;break;}
                if(!Fits)continue;

                TArray<FCrossing> Crossings;
                auto AddCrossing=[&](FVector2D Center,bool AlongX)
                {
                    const FVector2D Half=AlongX ? FVector2D(Approach,BridgeHalfWidth):FVector2D(BridgeHalfWidth,Approach);
                    const double Clearance=AlongX ? Y-FMath::Abs(Center.Y):X-FMath::Abs(Center.X);
                    if(Clearance<Bank+BridgeHalfWidth+45 || !Clear(FBox2D(Center-Half,Center+Half)) ||
                        !Flat(FBox2D(Center-Half,Center+Half)))return false;
                    for(const auto& Existing:Crossings)
                    {
                        if(Existing.AlongX!=AlongX)continue;
                        if(Existing.Center.Equals(Center,.01))return true;
                        if(FVector2D::Distance(Existing.Center,Center)<BridgeHalfWidth*2+10)return false;
                    }
                    Crossings.Add({Center,AlongX});
                    return true;
                };
                auto CheckSegment=[&](const FVector& A,const FVector& B,bool Bridgeable)
                {
                    for(int32 Index=0;Index<4;++Index)
                    {
                        const auto& Channel=Banks[Index];
                        const FBox Margin(FVector(Channel.Min,-10000)-FVector(40,40,0),
                                          FVector(Channel.Max,10000)+FVector(40,40,0));
                        if(!FMath::LineBoxIntersection(Margin,A,B,B-A))continue;
                        const bool AlongX=Index>=2;
                        const int32 Axis=AlongX ? 0:1;
                        const double At=Channel.GetCenter()[Axis];
                        if(!Bridgeable || FMath::Min(A[Axis],B[Axis])>At-Approach ||
                            FMath::Max(A[Axis],B[Axis])<At+Approach ||
                            FMath::Abs(A[1-Axis]-B[1-Axis])>.01)return false;
                        const FVector2D Center=AlongX ? FVector2D(At,A.Y):FVector2D(A.X,At);
                        if(!AddCrossing(Center,AlongX) || !AddCrossing(Reflect(Center),AlongX))return false;
                    }
                    return true;
                };
                for(const auto* Paths:{&L.Routes,&L.CrossingRoutes,&L.StairRoutes,&L.VillageSideBypasses,&L.SpawnExitRoutes})
                    for(const auto& Path:*Paths)
                        for(int32 I=1;Fits && I<Path.Num();++I)
                            Fits=CheckSegment(Path[I-1],Path[I],Paths==&L.Routes || Paths==&L.CrossingRoutes);
                for(const auto& Site:L.CoverSites)if(Fits)Fits=CheckSegment(Site.Entry,Site.Hide,false);
                if(!Fits)continue;

                // Give each bank a crossing, even where no authored route
                // enters the ring. Sample one bank and reflect the result.
                for(bool AlongX:{false,true})
                {
                    const double Limit=(AlongX ? Y:X)-Bank-BridgeHalfWidth-45;
                    const double BankCenter=AlongX ? -X:-Y;
                    bool Connected=Crossings.ContainsByPredicate([&](const FCrossing& C)
                    {
                        return C.AlongX==AlongX && C.Center[AlongX ? 0:1]==BankCenter;
                    });
                    for(double Offset=0;!Connected && Offset<=Limit;Offset+=100)
                        for(double Sign:{-1.,1.})
                        {
                            const FVector2D Center=AlongX ? FVector2D(BankCenter,Sign*Offset):FVector2D(Sign*Offset,BankCenter);
                            const int32 Previous=Crossings.Num();
                            Connected=AddCrossing(Center,AlongX) && AddCrossing(Reflect(Center),AlongX);
                            if(Connected)break;
                            Crossings.SetNum(Previous);
                        }
                    if(!Connected){Fits=false;break;}
                }
                if(!Fits)continue;

                // Round the corners and vary the banks between bridge approaches.
                // Keep the same cross-section beneath each bridge.
                constexpr double Radius=205;
                const FVector2D Corners[]{{X,-Y},{X,Y},{-X,Y},{-X,-Y}};
                const FVector2D Directions[]{{1,0},{0,1},{-1,0},{0,-1}};
                auto Ease=[](double Alpha){Alpha=FMath::Clamp(Alpha,0.,1.);return Alpha*Alpha*(3-2*Alpha);};
                for(double Amplitude:{155.,110.,65.,35.})
                {
                    TArray<FVector2D> Centerline;
                    for(int32 Side=0;Side<4;++Side)
                    {
                        const auto& Direction=Directions[Side];
                        const FVector2D Normal(Direction.Y,-Direction.X);
                        const FVector2D A=Corners[(Side+3)%4]+Direction*Radius,B=Corners[Side]-Direction*Radius;
                        const double Length=FVector2D::Distance(A,B);
                        const int32 Samples=FMath::CeilToInt32(Length/40);
                        for(int32 I=0;I<=Samples;++I)
                        {
                            const double T=double(I)/Samples;
                            auto P=FMath::Lerp(A,B,T);
                            double Blend=Ease(FMath::Min(T,1-T)*Length/240);
                            for(const auto& Crossing:Crossings)
                                if(Crossing.AlongX==((Side%2)!=0))
                                    Blend=FMath::Min(Blend,Ease((FVector2D::Distance(P,Crossing.Center)-125)/310));
                            const double Phase=Mirror && Side==2 ? 1-T:T;
                            const double Wave=Mirror && (Side%2)!=0 ?
                                FMath::Cos(Phase*4*UE_PI):FMath::Sin(Phase*4*UE_PI+(Side%2)*.8);
                            P+=Normal*(Amplitude*Blend*FMath::Max(-.35,Wave));
                            Centerline.Add(P);
                        }
                        const FVector2D ArcCenter=Corners[Side]-Direction*Radius-Normal*Radius;
                        for(int32 I=1;I<9;++I)
                        {
                            const double Angle=I*UE_HALF_PI/9;
                            Centerline.Add(ArcCenter+Radius*(Normal*FMath::Cos(Angle)+Direction*FMath::Sin(Angle)));
                        }
                    }

                    const int32 Count=Centerline.Num();
                    auto& Inner=L.VillageRingInner;
                    auto& Outer=L.VillageRingOuter;
                    Inner.Reset(Count);
                    Outer.Reset(Count);
                    for(int32 I=0;I<Count;++I)
                    {
                        const FVector2D Tangent=(Centerline[(I+1)%Count]-Centerline[(I+Count-1)%Count]).GetSafeNormal();
                        const FVector2D Offset(Tangent.Y*Bank,-Tangent.X*Bank);
                        Inner.Add(Centerline[I]-Offset);
                        Outer.Add(Centerline[I]+Offset);
                    }
                    bool CurvesFit=true;
                    TArray<FBox2D> Bounds;
                    for(int32 I=0;I<Count && CurvesFit;++I)
                    {
                        const int32 J=(I+1)%Count;
                        FBox2D Strip(ForceInit);
                        Strip+=Inner[I];Strip+=Inner[J];Strip+=Outer[I];Strip+=Outer[J];
                        if(!Clear(Strip) || !Flat(Strip) ||
                            Strip.Min.X<-HX+45 || Strip.Max.X>HX-45 ||
                            Strip.Min.Y<-HY+45 || Strip.Max.Y>HY-45)
                        {
                            CurvesFit=false;
                            break;
                        }
                        const FBox Margin(FVector(Strip.Min,-10000)-FVector(35,35,0),
                                          FVector(Strip.Max,10000)+FVector(35,35,0));
                        auto ClearRoute=[&](const FVector& Start,const FVector& End,bool Bridgeable)
                        {
                            if(!FMath::LineBoxIntersection(Margin,Start,End,End-Start))return true;
                            return Bridgeable && Crossings.ContainsByPredicate([&](const FCrossing& C)
                            {
                                const int32 Axis=C.AlongX ? 0:1;
                                const double At=C.Center[Axis];
                                return FMath::IsNearlyEqual(Start[1-Axis],C.Center[1-Axis],.01) &&
                                    FMath::IsNearlyEqual(End[1-Axis],C.Center[1-Axis],.01) &&
                                    FMath::Min(Start[Axis],End[Axis])<=At-Approach &&
                                    FMath::Max(Start[Axis],End[Axis])>=At+Approach &&
                                    Strip.Min[Axis]>=At-DeckEnd && Strip.Max[Axis]<=At+DeckEnd;
                            });
                        };
                        for(const auto* Paths:{&L.Routes,&L.CrossingRoutes,&L.StairRoutes,&L.VillageSideBypasses,&L.SpawnExitRoutes})
                            for(const auto& Path:*Paths)
                                for(int32 K=1;CurvesFit && K<Path.Num();++K)
                                    CurvesFit=ClearRoute(Path[K-1],Path[K],Paths==&L.Routes || Paths==&L.CrossingRoutes);
                        for(const auto& Site:L.CoverSites)if(CurvesFit)CurvesFit=ClearRoute(Site.Entry,Site.Hide,false);
                        Bounds.Add(Strip);
                    }
                    for(const auto& Block:L.VillageBlocks)
                    {
                        const FVector2D Center(Block.Frame.GetLocation());
                        for(double DX:{-.5,.5})for(double DY:{-.5,.5})
                            CurvesFit &= VillageRingContains(L,Center+FVector2D(DX*Block.Size.X,DY*Block.Size.Y),true);
                    }
                    const double Bed=VillageGroundHeight(L,{0,0})-Lip-Depth;
                    if(CurvesFit && TriangulateVillageWaterways(L,Bed))
                    {
                        for(const auto& Strip:Bounds)Water(Strip);
                        break;
                    }
                    Inner.Reset();
                    Outer.Reset();
                }
                if(L.VillageRingInner.IsEmpty())continue;
                for(const auto& Crossing:Crossings)Bridge(Crossing.Center,Crossing.AlongX);
                auto LiftCrossings=[&](TArray<FVector>& Path)
                {
                    if(Path.IsEmpty())return;
                    TArray<FVector> Adjusted{Path[0]};
                    for(int32 I=1;I<Path.Num();++I)
                    {
                        const FVector A=Path[I-1],B=Path[I];
                        TArray<TPair<double,double>> Grades;
                        for(const auto& Crossing:Crossings)
                        {
                            const int32 Axis=Crossing.AlongX ? 0:1;
                            const double At=Crossing.Center[Axis];
                            if(FMath::Abs(A[1-Axis]-Crossing.Center[1-Axis])>.01 ||
                                FMath::Abs(B[1-Axis]-Crossing.Center[1-Axis])>.01 ||
                                FMath::Min(A[Axis],B[Axis])>At-Approach ||
                                FMath::Max(A[Axis],B[Axis])<At+Approach)continue;
                            for(double Offset:{-Approach,-DeckEnd,DeckEnd,Approach})
                                Grades.Emplace((At+Offset-A[Axis])/(B[Axis]-A[Axis]),
                                    FMath::Abs(Offset)<=DeckEnd ? Rise:0.);
                        }
                        Grades.Sort([](const auto& A,const auto& B){return A.Key<B.Key;});
                        for(const auto& Grade:Grades)
                            if(Grade.Key>0 && Grade.Key<1)
                                Adjusted.Add(FMath::Lerp(A,B,Grade.Key)+FVector(0,0,Grade.Value));
                        Adjusted.Add(B);
                    }
                    Path=MoveTemp(Adjusted);
                };
                for(auto* Paths:{&L.Routes,&L.CrossingRoutes})
                    for(auto& Path:*Paths)LiftCrossings(Path);
                LiftCrossings(L.LowerRoute);
                L.FeatureNames.Add(TEXT("VillageLoopStreams"));
                return true;
            }
        }
        return false;
    };
    const bool PreferLoop=WaterRng.RandRange(0,1)==0;
    if(!(PreferLoop ? (Loop() || Transverse()):(Transverse() || Loop())))return;
    L.GradedTerrain=true;
    L.FeatureNames.Add(TEXT("VillageWatercourses"));
    // Keep the original continuous horizon, below the lowest possible bed
    // so its visual-only ground slab cannot show through the water.
    if(Mirror)for(auto& Piece:L.FixedPieces)
        if(Piece.VisualOnly && !Piece.Mesh && Piece.Transform.GetScale3D().X>1000)
            Piece.Transform.AddToTranslation({0,0,-60});
}
}
