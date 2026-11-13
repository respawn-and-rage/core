#include "ERMapLighting.h"
#include "ERProceduralMap.h"

namespace
{
struct FLampBuilder
{
    TArray<FERCellPanelDesc>& Panels;
    TArray<FERMapLamp>& Lamps;
    UMaterialInterface* LampSurface;
    float CellSize;
    int32 NextId = 0;

    FLampBuilder(TArray<FERCellPanelDesc>& InPanels, TArray<FERMapLamp>& InLamps,
        UMaterialInterface* InSurface, float InCellSize)
        : Panels(InPanels), Lamps(InLamps), LampSurface(InSurface), CellSize(InCellSize)
    {
        for (const auto& Panel : Panels) NextId = FMath::Max(NextId, Panel.Id + 1);
    }

    int32 AddPanel(const FVector& Center, const FQuat& Rotation, const FVector& Size, bool Lens,
        UMaterialInterface* Surface = nullptr)
    {
        const int32 Index = Panels.Num();
        auto& Panel = Panels.AddDefaulted_GetRef();
        Panel.Id = NextId++;
        Panel.Transform = FTransform(Rotation, Center - Rotation.RotateVector(Size * .5));
        Panel.Size = Size;
        Panel.Material = Lens ? EERBlockMaterial::Glass : EERBlockMaterial::Metal;
        Panel.Cell = Lens ? Size : FVector(FERCellWorld::FitCell(Size.X, CellSize),
            FERCellWorld::FitCell(Size.Y, CellSize), FERCellWorld::FitCell(Size.Z, CellSize));
        Panel.bPane = Lens;
        Panel.bAnchored = Lens;
        Panel.bAnchorBottom = !Lens;
        Panel.Surface = Lens ? LampSurface : Surface;
        Panel.Strength = Lens ? 1.f : 4.f;
        return Index;
    }

    FERMapLamp& Ceiling(const FVector& Center)
    {
        auto& Lamp = Lamps.AddDefaulted_GetRef();
        Lamp.Lenses.Add(AddPanel(Center, FQuat::Identity, {60,60,4}, true));
        Lamp.Position = Center - FVector(0,0,12);
        return Lamp;
    }

    void CeilingGrid(FVector Center, FVector2D Size, FQuat Frame = FQuat::Identity,
        const TArray<FERLayoutFixedPiece>* Mounts = nullptr, TArray<FERMapLightZone>* HallZones = nullptr)
    {
        const int32 Columns = FMath::Clamp(FMath::CeilToInt32(Size.X / 1100.), 1, 8);
        const int32 Rows = FMath::Clamp(FMath::CeilToInt32(Size.Y / 1100.), 1, 8);
        const int32 ZoneColumns = (Columns + 1) / 2;
        if (HallZones) HallZones->SetNum(ZoneColumns * ((Rows + 1) / 2));
        for (int32 Y = 0; Y < Rows; ++Y)
            for (int32 X = 0; X < Columns; ++X)
            {
                FVector Point = Center + Frame.RotateVector(FVector((X+.5)/Columns*Size.X - Size.X*.5,
                    (Y+.5)/Rows*Size.Y - Size.Y*.5, 0));
                if (Mounts)
                {
                    // Skip stair openings and terraces. Use the underside of the actual slab.
                    const auto* Slab = Mounts->FindByPredicate([&](const FERLayoutFixedPiece& Piece)
                    {
                        if (Piece.Mesh || Piece.VisualOnly || Piece.CollisionOnly || Piece.PawnBarrier) return false;
                        const FVector Half = Piece.Transform.GetScale3D() * 50.;
                        const FVector Local = Piece.Transform.InverseTransformPositionNoScale(Point);
                        return Half.Z <= 55. && FMath::Abs(Local.Z + Half.Z) <= 8. &&
                            FMath::Abs(Local.X) + 60. <= Half.X && FMath::Abs(Local.Y) + 60. <= Half.Y;
                    });
                    if (!Slab) continue;
                    Point.Z = Slab->Transform.GetLocation().Z - Slab->Transform.GetScale3D().Z * 50. - 3.;
                }
                auto& Lamp = Ceiling(Point);
                if (HallZones)
                {
                    Lamp.Candelas = 0.f;
                    (*HallZones)[(Y / 2) * ZoneColumns + X / 2].Lamps.Add(Lamps.Num() - 1);
                }
            }
        if (!HallZones) return;
        for (auto& Zone : *HallZones)
        {
            FBox Bounds(ForceInit);
            for (int32 Index : Zone.Lamps) Bounds += Lamps[Index].Position;
            const FVector Extent = Bounds.GetExtent() + FVector(Size.X / Columns, Size.Y / Rows, 0) * .5;
            FVector Fill = Bounds.GetCenter();
            Fill.Z *= .62;
            Zone.FillPositions.Add(Fill);
            Zone.Radius = float(FVector(Extent.X, Extent.Y, Fill.Z).Size() * 1.18);
            Zone.Intensity = .85f;
        }
    }

    void Perimeter(const FVector& Arena, bool Circular, UMaterialInterface* Steel, TArray<FERMapLightZone>& Zones)
    {
        constexpr double Top = 1400;
        constexpr double PoleBottom = -500;
        constexpr double PoleHeight = Top - PoleBottom;
        for (int32 Index = 0; Index < 6; ++Index)
        {
            const double Angle = Index * UE_DOUBLE_PI / 3.;
            FVector Unit(FMath::Cos(Angle), FMath::Sin(Angle), 0);
            if (!Circular) Unit /= FMath::Max(FMath::Abs(Unit.X), FMath::Abs(Unit.Y));
            const FVector Head(Unit.X * (Arena.X*.5+65), Unit.Y * (Arena.Y*.5+65), Top);
            auto& Lamp = Lamps.AddDefaulted_GetRef();
            Lamp.Pole = AddPanel(Head - FVector(0,0,PoleHeight*.5), FQuat::Identity, {30,30,PoleHeight}, false, Steel);
            const FRotator Aim = (FVector(0,0,30) - (Head + FVector(0,0,66))).Rotation();
            const FQuat Frame = FRotator(FMath::Clamp(Aim.Pitch, -24., -12.), Aim.Yaw, 0).Quaternion();
            // Tilt around the bottom edge, embedded slightly in the steel post for a structural joint.
            const FVector PanelCenter = Head + Frame.GetUpVector() * 62.;
            Lamp.Candelas = 0.f;
            const double SectorDepth = Head.Size2D() * .44;
            auto& Zone = Zones.AddDefaulted_GetRef();
            Zone.Lamps.Add(Lamps.Num() - 1);
            Zone.FillPositions.Add(FVector(Head.X, Head.Y, Top * .65) - Head.GetSafeNormal2D() * SectorDepth);
            Zone.Radius = float(FMath::Max(1800., SectorDepth * 1.85));
            Zone.Intensity = 1.15f;
            Zone.Color = FLinearColor(.76f, .85f, 1.f);
            // A steel square backing joins the four standard 60 cm lenses.
            Panels[AddPanel(PanelCenter, Frame, {15,132,132}, false, Steel)].bAnchorBottom = false;
            for (double Y : {-33.,33.})
                for (double Z : {-33.,33.})
                {
                    const int32 Lens = AddPanel(PanelCenter + Frame.RotateVector(FVector(9.5,Y,Z)), Frame, {4,60,60}, true);
                    Panels[Lens].bAnchored = false;
                    Lamp.Lenses.Add(Lens);
                }
        }
    }
};

void BuildRoomsZones(const FERProceduralLayout& Layout, const TArray<FERMapLamp>& Lamps, TArray<FERMapLightZone>& Zones)
{
    // Reuse the final room graph so fixtures follow the selected seed and symmetry.
    const int32 RoomCount = Layout.RoomNodes.Num();
    Zones.SetNum(RoomCount + Layout.RoomLinks.Num());
    for (int32 Index = 0; Index < Lamps.Num(); ++Index)
    {
        const FVector Position = Lamps[Index].Position + FVector(0, 0, 17);
        int32 BestZone = INDEX_NONE;
        double BestDistance = TNumericLimits<double>::Max();
        auto Consider = [&](int32 Zone, const FVector& RoofPoint)
        {
            const double Distance = FVector::DistSquared(Position, RoofPoint);
            if (Distance < BestDistance)
            {
                BestDistance = Distance;
                BestZone = Zone;
            }
        };
        for (int32 RoomIndex = 0; RoomIndex < RoomCount; ++RoomIndex)
        {
            const auto& Room = Layout.RoomNodes[RoomIndex];
            if (Room.Junction) continue;
            const FVector2D Half = Room.Size * .5;
            Consider(RoomIndex, FVector(
                FMath::Clamp(Position.X, Room.Center.X - Half.X, Room.Center.X + Half.X),
                FMath::Clamp(Position.Y, Room.Center.Y - Half.Y, Room.Center.Y + Half.Y),
                Room.Center.Z + Room.Height));
        }
        for (int32 LinkIndex = 0; LinkIndex < Layout.RoomLinks.Num(); ++LinkIndex)
        {
            const auto& Link = Layout.RoomLinks[LinkIndex];
            const FVector FlatDelta(Link.End.X - Link.Start.X, Link.End.Y - Link.Start.Y, 0);
            if (FlatDelta.SizeSquared() < 100.) continue;
            const double T = FMath::Clamp(FVector::DotProduct(Position - Link.Start, FlatDelta) / FlatDelta.SizeSquared(), 0., 1.);
            Consider(RoomCount + LinkIndex, FMath::Lerp(Link.Start, Link.End, T) + FVector(0, 0, Link.Height));
        }
        if (Zones.IsValidIndex(BestZone)) Zones[BestZone].Lamps.Add(Index);
    }
    for (int32 RoomIndex = 0; RoomIndex < RoomCount; ++RoomIndex)
    {
        auto& Zone = Zones[RoomIndex];
        if (Zone.Lamps.IsEmpty()) continue;
        const auto& Room = Layout.RoomNodes[RoomIndex];
        const double Height = Room.Height * .62;
        Zone.FillPositions.Add(Room.Center + FVector(0, 0, Height));
        Zone.Radius = float(FVector(Room.Size * .5, Height).Size() * 1.12);
        Zone.Intensity = 1.15f;
    }
    for (int32 LinkIndex = 0; LinkIndex < Layout.RoomLinks.Num(); ++LinkIndex)
    {
        auto& Zone = Zones[RoomCount + LinkIndex];
        if (Zone.Lamps.IsEmpty()) continue;
        const auto& Link = Layout.RoomLinks[LinkIndex];
        const double Length = FVector::Dist2D(Link.Start, Link.End);
        const int32 Segments = FMath::Clamp(FMath::CeilToInt32(Length / 1400.), 1, Zone.Lamps.Num());
        const double Height = Link.Height * .62;
        for (int32 Index = 0; Index < Segments; ++Index)
            Zone.FillPositions.Add(FMath::Lerp(Link.Start, Link.End, (Index + .5) / Segments) + FVector(0, 0, Height));
        Zone.Radius = float(FVector(Length / (2 * Segments), Link.Width * .5, Height).Size() * 1.18);
        Zone.Intensity = .95f;
    }
    Zones.RemoveAll([](const FERMapLightZone& Zone) { return Zone.Lamps.IsEmpty(); });
}
}

int32 FERMapLamp::WorkingLenses(const FERCellWorld& Cells) const
{
    if (Pole != INDEX_NONE)
    {
        const auto& Panel = Cells.GetPanels()[Pole];
        bool TopAlive = false;
        for (int32 Y = 0; Y < Panel.Dims.Y && !TopAlive; ++Y)
            for (int32 X = 0; X < Panel.Dims.X && !TopAlive; ++X)
                TopAlive = Cells.IsAlive(Pole, X, Y, Panel.Dims.Z-1);
        if (!TopAlive) return 0;
    }
    int32 Count = 0;
    for (const int32 Lens : Lenses) Count += Cells.IsAlive(Lens, 0, 0, 0) ? 1 : 0;
    return Count;
}

float FERMapLightZone::PoweredFraction(const TArray<FERMapLamp>& MapLamps, const FERCellWorld& Cells) const
{
    if (Lamps.IsEmpty()) return 0.f;
    int32 Working = 0;
    int32 Total = 0;
    for (int32 Index : Lamps)
    {
        const auto& Lamp = MapLamps[Index];
        Working += Lamp.WorkingLenses(Cells);
        Total += Lamp.Lenses.Num();
    }
    return Total > 0 ? float(Working) / float(Total) : 0.f;
}

void ERMapLighting::Build(FERProceduralLayout& Layout, const UERMapTemplate& Recipe, float CellSize,
    UMaterialInterface* LampSurface, bool Night, TArray<FERMapLamp>& Lamps, TArray<FERMapLightZone>& Zones)
{
    FLampBuilder Builder(Layout.Panels, Lamps, LampSurface, CellSize);
    if (Recipe.Archetype == EERMapArchetype::Rooms)
    {
        Layout.FixedPieces.RemoveAll([&](const FERLayoutFixedPiece& Piece)
        {
            if (!Piece.VisualOnly || !Piece.Surface || Piece.Surface != Recipe.Rooms.LampSurface) return false;
            auto& Lamp = Builder.Ceiling(Piece.Transform.GetLocation());
            Lamp.Candelas = 0.f;
            return true;
        });
        BuildRoomsZones(Layout, Lamps, Zones);
        return;
    }
    if (Recipe.Archetype == EERMapArchetype::FireTraining)
    {
        const float Height = Layout.IndoorCeiling;
        Builder.CeilingGrid({0,0,Height-5}, {Layout.WarehouseWidth,Layout.WarehouseLength},
            FQuat::Identity, nullptr, &Zones);
        return;
    }
    if (Night)
    {
        auto* Steel = Layout.ThemeMetalDetails;
        if (!Steel && Layout.ThemeMaterials.IsValidIndex(int32(EERBlockMaterial::Metal)))
            Steel = Layout.ThemeMaterials[int32(EERBlockMaterial::Metal)];
        if (!Steel) Steel = Recipe.CentralBuilding.SteelSurface.Get();
        Builder.Perimeter(Layout.ArenaSize, Layout.CircularArena, Steel, Zones);
    }
    const int32 FirstCeilingLamp = Lamps.Num();
    for (const FVector& Mount : Layout.CentralCeilingMounts) Builder.Ceiling(Mount);
    if (Recipe.Archetype == EERMapArchetype::CentralBuilding && !Layout.CircularArena)
        for (int32 Floor = 0; Floor < Layout.BuildingFloors; ++Floor)
        {
            const FVector2D Setback = Layout.TerracedBuilding ? FVector2D(Floor*500.,Floor*200.) : FVector2D::ZeroVector;
            const FVector2D Size = FVector2D(Layout.CentralBuildingSize) - Setback - FVector2D(600,120);
            Builder.CeilingGrid(Layout.CentralBuildingCenter + FVector(0,0,(Floor+1)*320.-24.), Size,
                FQuat::Identity, &Layout.FixedPieces);
        }
    for (const auto& Building : Layout.SecondaryStructures)
    {
        double SecondaryOffset = 8.;
        if (Recipe.Archetype == EERMapArchetype::CentralBuilding)
            SecondaryOffset = Layout.CircularArena ? Building.Size.Z-300.+3. : 6.;
        for (int32 Floor = 1; Floor <= Building.Floors; ++Floor)
            Builder.CeilingGrid(Building.Frame.TransformPosition({0,0,Building.Size.Z*Floor/Building.Floors-SecondaryOffset}),
                FVector2D(Building.Size), Building.Frame.GetRotation());
    }
    for (const auto& Base : Layout.Bases)
        Builder.CeilingGrid(Base.Point({0,0,Base.RoofZ-10.}), FVector2D(Base.Size), Base.Frame.GetRotation());
    for (const auto& Bay : Layout.VillageClosedGround)
    {
        FVector2D Center = FVector2D::ZeroVector;
        for (const FVector2D& Point : Bay) Center += Point;
        if (!Bay.IsEmpty())
        {
            Center /= Bay.Num();
            Builder.Ceiling(FVector(Center, Recipe.Village.DeckHeight - 12.));
        }
    }
    for (const auto& House : Layout.Houses)
    {
        const FVector Size = House.Frame.GetScale3D();
        Builder.CeilingGrid(House.Frame.TransformPositionNoScale({0,0,Size.Z-26.}), FVector2D(Size),
            House.Frame.GetRotation());
    }
    if (Recipe.Archetype == EERMapArchetype::UrbanTown)
        for (const auto& Piece : Layout.FixedPieces)
    {
        if (!Piece.Roof || Piece.VisualOnly || Piece.Mesh) continue;
        const FVector Size = Piece.Transform.GetScale3D() * 100.;
        Builder.CeilingGrid(Piece.Transform.GetLocation() - FVector(0,0,Size.Z*.5+6.),
            FVector2D(Size), Piece.Transform.GetRotation());
    }
    if (Night)
        for (int32 Index = FirstCeilingLamp; Index < Lamps.Num(); ++Index)
        {
            auto& Lamp = Lamps[Index];
            Lamp.Candelas = 0.f;
            auto& Zone = Zones.AddDefaulted_GetRef();
            Zone.Lamps.Add(Index);
            Zone.FillPositions.Add(Lamp.Position - FVector(0, 0, 90));
            Zone.Radius = 500.f;
            Zone.Intensity = .35f;
        }
}

void ERMapLighting::BuildDepot(float CellSize, UMaterialInterface* LampSurface,
    TArray<FERCellPanelDesc>& Panels, TArray<FERMapLamp>& Lamps, TArray<FERMapLightZone>& Zones)
{
    FLampBuilder Builder(Panels, Lamps, LampSurface, CellSize);
    Builder.Perimeter({5600,3600,0}, false, nullptr, Zones);
}
