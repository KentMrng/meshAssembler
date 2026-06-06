#pragma once

#include <maya/MPxNode.h>
#include <maya/MDataBlock.h>
#include <maya/MObject.h>
#include <maya/MPlug.h>
#include <maya/MPlugArray.h>
#include <maya/MPoint.h>
#include <maya/MPointArray.h>
#include <maya/MStatus.h>
#include <maya/MString.h>
#include <maya/MTypeId.h>

#include <cstdint>
#include <cstddef>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <vector>

class MeshAssemblerNode : public MPxNode
{
public:
    MeshAssemblerNode() = default;
    ~MeshAssemblerNode() override = default;

    MStatus compute(const MPlug& plug, MDataBlock& dataBlock) override;
    MStatus setDependentsDirty(const MPlug& plug, MPlugArray& affectedPlugs) override;
    SchedulingType schedulingType() const override { return MPxNode::kParallel; }

    static void* creator() { return new MeshAssemblerNode(); }
    static MStatus initialize();

    static const MString kNodeName;
    // Replace with your own registered ID before shipping.
    static const MTypeId kNodeId;

    static MObject aTargetOrigMesh;
    static MObject aSource;
    static MObject aSourceMesh;
    static MObject aSourceOrigMesh;
    static MObject aMatchTolerance;
    static MObject aRebuildTrigger;
    static MObject aOutputMesh;

private:
    struct MapSource
    {
        unsigned int sourceLogicalIndex = 0;
        unsigned int vertexIndex = 0;
    };

    struct MatchCandidate
    {
        unsigned int sourceLogicalIndex = 0;
        unsigned int vertexIndex = 0;
        double distanceSquared = 0.0;
    };

    struct GridKey
    {
        std::int64_t x = 0;
        std::int64_t y = 0;
        std::int64_t z = 0;

        bool operator==(const GridKey& rhs) const
        {
            return x == rhs.x && y == rhs.y && z == rhs.z;
        }
    };

    struct GridKeyHash
    {
        std::size_t operator()(const GridKey& k) const;
    };

    using SpatialHash = std::unordered_map<GridKey, std::vector<MatchCandidate>, GridKeyHash>;

    struct SourcePointArray
    {
        unsigned int logicalIndex = 0;
        MPointArray points;
    };

    MStatus rebuildMappingCache(MDataBlock& dataBlock,
                                const MObject& targetOrigMeshObj,
                                double matchTolerance,
                                int rebuildTrigger);

    MStatus readSourcePoints(MDataBlock& dataBlock,
                             MObject childMeshAttribute,
                             std::vector<SourcePointArray>& outSources) const;

    static GridKey makeGridKey(const MPoint& p, double cellSize);
    static double distanceSquared(const MPoint& a, const MPoint& b);

    static MPoint averageMappedSources(const std::vector<MapSource>& sources,
                                       const std::unordered_map<unsigned int, const MPointArray*>& sourcePointMap,
                                       unsigned int start,
                                       unsigned int end,
                                       const MPoint& fallback);

private:
    mutable std::mutex m_cacheMutex;
    bool m_cacheDirty = true;
    bool m_cacheValid = false;
    double m_cachedMatchTolerance = -1.0;
    int m_cachedRebuildTrigger = std::numeric_limits<int>::min();

    MPointArray m_targetOrigPoints;
    std::vector<unsigned int> m_offsets; // size = target vertex count + 1
    std::vector<MapSource> m_sources;
};
