#include "MeshAssemblerNode.h"

#include <maya/MArrayDataHandle.h>
#include <maya/MDataHandle.h>
#include <maya/MFnCompoundAttribute.h>
#include <maya/MFnMesh.h>
#include <maya/MFnMeshData.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MGlobal.h>
#include <maya/MPlugArray.h>

#include <algorithm>
#include <cmath>
#include <sstream>

namespace
{
constexpr double kDefaultMatchTolerance = 1.0e-4;
constexpr double kMinimumMatchTolerance = 1.0e-10;
}

std::size_t MeshAssemblerNode::GridKeyHash::operator()(const GridKey& k) const
{
    // Large odd constants; sufficient for a spatial hash key.
    const std::uint64_t hx = static_cast<std::uint64_t>(k.x) * 73856093ull;
    const std::uint64_t hy = static_cast<std::uint64_t>(k.y) * 19349663ull;
    const std::uint64_t hz = static_cast<std::uint64_t>(k.z) * 83492791ull;
    return static_cast<std::size_t>(hx ^ hy ^ hz);
}

MeshAssemblerNode::GridKey MeshAssemblerNode::makeGridKey(const MPoint& p, double cellSize)
{
    return GridKey{
        static_cast<std::int64_t>(std::floor(p.x / cellSize)),
        static_cast<std::int64_t>(std::floor(p.y / cellSize)),
        static_cast<std::int64_t>(std::floor(p.z / cellSize))
    };
}

double MeshAssemblerNode::distanceSquared(const MPoint& a, const MPoint& b)
{
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    const double dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

MPoint MeshAssemblerNode::averageMappedSources(const std::vector<MapSource>& sources,
                                                const std::unordered_map<unsigned int, const MPointArray*>& sourcePointMap,
                                                unsigned int start,
                                                unsigned int end,
                                                const MPoint& fallback)
{
    MPoint sum(0.0, 0.0, 0.0);
    unsigned int validCount = 0;

    for (unsigned int s = start; s < end; ++s)
    {
        const MapSource& src = sources[s];
        const auto it = sourcePointMap.find(src.sourceLogicalIndex);
        if (it == sourcePointMap.end() || it->second == nullptr)
        {
            continue;
        }

        const MPointArray& pts = *(it->second);
        if (src.vertexIndex >= pts.length())
        {
            continue;
        }

        sum += pts[src.vertexIndex];
        ++validCount;
    }

    if (validCount == 0)
    {
        return fallback;
    }

    return sum / static_cast<double>(validCount);
}

const MString MeshAssemblerNode::kNodeName("meshAssembler");
const MTypeId MeshAssemblerNode::kNodeId(0x0013F001);

MObject MeshAssemblerNode::aTargetOrigMesh;
MObject MeshAssemblerNode::aSource;
MObject MeshAssemblerNode::aSourceMesh;
MObject MeshAssemblerNode::aSourceOrigMesh;
MObject MeshAssemblerNode::aMatchTolerance;
MObject MeshAssemblerNode::aRebuildTrigger;
MObject MeshAssemblerNode::aOutputMesh;

MStatus MeshAssemblerNode::initialize()
{
    MStatus status;

    MFnTypedAttribute tAttr;
    MFnNumericAttribute nAttr;
    MFnCompoundAttribute cAttr;

    aTargetOrigMesh = tAttr.create("targetOrigMesh", "tom", MFnData::kMesh, MObject::kNullObj, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    tAttr.setReadable(false);
    tAttr.setWritable(true);
    tAttr.setStorable(true);
    tAttr.setCached(false);
    addAttribute(aTargetOrigMesh);

    aSourceMesh = tAttr.create("mesh", "m", MFnData::kMesh, MObject::kNullObj, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    tAttr.setReadable(false);
    tAttr.setWritable(true);
    tAttr.setStorable(true);
    tAttr.setCached(false);

    aSourceOrigMesh = tAttr.create("origMesh", "om", MFnData::kMesh, MObject::kNullObj, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    tAttr.setReadable(false);
    tAttr.setWritable(true);
    tAttr.setStorable(true);
    tAttr.setCached(false);

    aSource = cAttr.create("source", "src", &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    cAttr.setArray(true);
    cAttr.setUsesArrayDataBuilder(true);
    cAttr.setReadable(false);
    cAttr.setWritable(true);
    cAttr.setStorable(true);
    cAttr.addChild(aSourceOrigMesh);
    cAttr.addChild(aSourceMesh);
    addAttribute(aSource);

    aMatchTolerance = nAttr.create("matchTolerance", "mtol", MFnNumericData::kDouble, kDefaultMatchTolerance, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    nAttr.setMin(0.0);
    nAttr.setReadable(true);
    nAttr.setWritable(true);
    nAttr.setStorable(true);
    addAttribute(aMatchTolerance);

    // Increment this value to force mapping rebuild without changing input meshes.
    aRebuildTrigger = nAttr.create("rebuildTrigger", "rbt", MFnNumericData::kInt, 0, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    nAttr.setReadable(true);
    nAttr.setWritable(true);
    nAttr.setStorable(true);
    addAttribute(aRebuildTrigger);

    aOutputMesh = tAttr.create("outputMesh", "out", MFnData::kMesh, MObject::kNullObj, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    tAttr.setReadable(true);
    tAttr.setWritable(false);
    tAttr.setStorable(false);
    addAttribute(aOutputMesh);

    attributeAffects(aTargetOrigMesh, aOutputMesh);
    attributeAffects(aSource, aOutputMesh);
    attributeAffects(aSourceMesh, aOutputMesh);
    attributeAffects(aSourceOrigMesh, aOutputMesh);
    attributeAffects(aMatchTolerance, aOutputMesh);
    attributeAffects(aRebuildTrigger, aOutputMesh);

    return MS::kSuccess;
}

MStatus MeshAssemblerNode::setDependentsDirty(const MPlug& plug, MPlugArray& affectedPlugs)
{
    const MObject attr = plug.attribute();

    // Only orig/template-side changes require mapping rebuild.
    // Live source mesh changes affect output but can reuse the existing mapping.
    if (attr == aTargetOrigMesh ||
        attr == aSourceOrigMesh ||
        attr == aMatchTolerance ||
        attr == aRebuildTrigger ||
        attr == aSource)
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        m_cacheDirty = true;
    }

    return MPxNode::setDependentsDirty(plug, affectedPlugs);
}

MStatus MeshAssemblerNode::readSourcePoints(MDataBlock& dataBlock,
                                             MObject childMeshAttribute,
                                             std::vector<SourcePointArray>& outSources) const
{
    MStatus status;
    outSources.clear();

    MArrayDataHandle sourceArray = dataBlock.inputArrayValue(aSource, &status);
    if (status != MS::kSuccess)
    {
        return status;
    }

    const unsigned int elementCount = sourceArray.elementCount(&status);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    for (unsigned int i = 0; i < elementCount; ++i)
    {
        status = sourceArray.jumpToArrayElement(i);
        if (status != MS::kSuccess)
        {
            continue;
        }

        const unsigned int logicalIndex = sourceArray.elementIndex(&status);
        if (status != MS::kSuccess)
        {
            continue;
        }

        MDataHandle sourceHandle = sourceArray.inputValue(&status);
        if (status != MS::kSuccess)
        {
            continue;
        }

        MObject meshObj = sourceHandle.child(childMeshAttribute).asMesh();
        if (meshObj.isNull())
        {
            continue;
        }

        MFnMesh meshFn(meshObj, &status);
        if (status != MS::kSuccess)
        {
            continue;
        }

        SourcePointArray source;
        source.logicalIndex = logicalIndex;
        status = meshFn.getPoints(source.points, MSpace::kObject);
        if (status != MS::kSuccess)
        {
            continue;
        }

        outSources.push_back(source);
    }

    return MS::kSuccess;
}

MStatus MeshAssemblerNode::rebuildMappingCache(MDataBlock& dataBlock,
                                        const MObject& targetOrigMeshObj,
                                        double matchTolerance,
                                        int rebuildTrigger)
{
    MStatus status;

    MFnMesh targetMeshFn(targetOrigMeshObj, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    MPointArray targetPoints;
    status = targetMeshFn.getPoints(targetPoints, MSpace::kObject);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    std::vector<SourcePointArray> sourceOrigPoints;
    status = readSourcePoints(dataBlock, aSourceOrigMesh, sourceOrigPoints);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    const double safeTolerance = std::max(matchTolerance, kMinimumMatchTolerance);
    const double matchToleranceSquared = safeTolerance * safeTolerance;
    const double cellSize = safeTolerance;

    SpatialHash spatialHash;
    spatialHash.reserve(targetPoints.length() * 2 + 128);

    for (const SourcePointArray& source : sourceOrigPoints)
    {
        const unsigned int pointCount = source.points.length();
        for (unsigned int v = 0; v < pointCount; ++v)
        {
            const GridKey key = makeGridKey(source.points[v], cellSize);
            spatialHash[key].push_back(MatchCandidate{source.logicalIndex, v, 0.0});
        }
    }

    std::vector<unsigned int> newOffsets;
    std::vector<MapSource> newMapSources;
    newOffsets.reserve(targetPoints.length() + 1);
    newMapSources.reserve(targetPoints.length());

    unsigned int unmatchedCount = 0;
    unsigned int averagedVertexCount = 0;

    newOffsets.push_back(0);

    for (unsigned int i = 0; i < targetPoints.length(); ++i)
    {
        const MPoint& p = targetPoints[i];
        const GridKey baseKey = makeGridKey(p, cellSize);

        // For each source, keep only the closest vertex. This avoids accidentally
        // averaging duplicated vertices from the same source at the same position.
        std::unordered_map<unsigned int, MatchCandidate> bestMatchCandidateBySource;

        for (int dz = -1; dz <= 1; ++dz)
        {
            for (int dy = -1; dy <= 1; ++dy)
            {
                for (int dx = -1; dx <= 1; ++dx)
                {
                    const GridKey key{baseKey.x + dx, baseKey.y + dy, baseKey.z + dz};
                    const auto bucketIt = spatialHash.find(key);
                    if (bucketIt == spatialHash.end())
                    {
                        continue;
                    }

                    for (const MatchCandidate& candidateBase : bucketIt->second)
                    {
                        const auto sourceIt = std::find_if(
                            sourceOrigPoints.begin(), sourceOrigPoints.end(),
                            [&](const SourcePointArray& source) { return source.logicalIndex == candidateBase.sourceLogicalIndex; });

                        if (sourceIt == sourceOrigPoints.end())
                        {
                            continue;
                        }

                        if (candidateBase.vertexIndex >= sourceIt->points.length())
                        {
                            continue;
                        }

                        const double d2 = distanceSquared(p, sourceIt->points[candidateBase.vertexIndex]);
                        if (d2 > matchToleranceSquared)
                        {
                            continue;
                        }

                        MatchCandidate candidate = candidateBase;
                        candidate.distanceSquared = d2;

                        const auto bestIt = bestMatchCandidateBySource.find(candidate.sourceLogicalIndex);
                        if (bestIt == bestMatchCandidateBySource.end() || d2 < bestIt->second.distanceSquared)
                        {
                            bestMatchCandidateBySource[candidate.sourceLogicalIndex] = candidate;
                        }
                    }
                }
            }
        }

        std::vector<MapSource> vertexMapSources;
        vertexMapSources.reserve(bestMatchCandidateBySource.size());
        for (const auto& kv : bestMatchCandidateBySource)
        {
            vertexMapSources.push_back(MapSource{kv.second.sourceLogicalIndex, kv.second.vertexIndex});
        }

        std::sort(vertexMapSources.begin(), vertexMapSources.end(), [](const MapSource& a, const MapSource& b) {
            if (a.sourceLogicalIndex != b.sourceLogicalIndex)
            {
                return a.sourceLogicalIndex < b.sourceLogicalIndex;
            }
            return a.vertexIndex < b.vertexIndex;
        });

        if (vertexMapSources.empty())
        {
            ++unmatchedCount;
        }
        else if (vertexMapSources.size() > 1)
        {
            ++averagedVertexCount;
        }

        newMapSources.insert(newMapSources.end(), vertexMapSources.begin(), vertexMapSources.end());
        newOffsets.push_back(static_cast<unsigned int>(newMapSources.size()));
    }

    m_targetOrigPoints = targetPoints;
    m_offsets.swap(newOffsets);
    m_sources.swap(newMapSources);
    m_cachedMatchTolerance = matchTolerance;
    m_cachedRebuildTrigger = rebuildTrigger;
    m_cacheDirty = false;
    m_cacheValid = true;

    std::ostringstream ss;
    ss << "meshAssembler: mapping rebuilt. targetVertices=" << targetPoints.length()
       << ", sources=" << m_sources.size()
       << ", unmatched=" << unmatchedCount
       << ", averaged=" << averagedVertexCount;
    MGlobal::displayInfo(ss.str().c_str());

    if (unmatchedCount > 0)
    {
        std::ostringstream warn;
        warn << "meshAssembler: " << unmatchedCount
             << " target vertices had no matching source orig vertex. They will keep targetOrigMesh positions.";
        MGlobal::displayWarning(warn.str().c_str());
    }

    return MS::kSuccess;
}

MStatus MeshAssemblerNode::compute(const MPlug& plug, MDataBlock& dataBlock)
{
    if (plug != aOutputMesh)
    {
        return MS::kUnknownParameter;
    }

    std::lock_guard<std::mutex> lock(m_cacheMutex);

    MStatus status;

    MObject targetOrigMeshObj = dataBlock.inputValue(aTargetOrigMesh, &status).asMesh();
    CHECK_MSTATUS_AND_RETURN_IT(status);

    if (targetOrigMeshObj.isNull())
    {
        return MS::kFailure;
    }

    const double matchTolerance = dataBlock.inputValue(aMatchTolerance, &status).asDouble();
    CHECK_MSTATUS_AND_RETURN_IT(status);

    const int rebuildTrigger = dataBlock.inputValue(aRebuildTrigger, &status).asInt();
    CHECK_MSTATUS_AND_RETURN_IT(status);

    if (m_cacheDirty || !m_cacheValid ||
        matchTolerance != m_cachedMatchTolerance || rebuildTrigger != m_cachedRebuildTrigger)
    {
        status = rebuildMappingCache(dataBlock, targetOrigMeshObj, matchTolerance, rebuildTrigger);
        CHECK_MSTATUS_AND_RETURN_IT(status);
    }

    if (!m_cacheValid)
    {
        return MS::kFailure;
    }

    std::vector<SourcePointArray> sourceMeshPoints;
    status = readSourcePoints(dataBlock, aSourceMesh, sourceMeshPoints);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    std::unordered_map<unsigned int, const MPointArray*> sourcePointMap;
    sourcePointMap.reserve(sourceMeshPoints.size());
    for (const SourcePointArray& source : sourceMeshPoints)
    {
        sourcePointMap[source.logicalIndex] = &source.points;
    }

    MPointArray outputPoints = m_targetOrigPoints;
    const unsigned int targetVertexCount = outputPoints.length();

    if (m_offsets.size() != static_cast<std::size_t>(targetVertexCount + 1))
    {
        MGlobal::displayWarning("meshAssembler: mapping size mismatch. Rebuilding mapping.");
        status = rebuildMappingCache(dataBlock, targetOrigMeshObj, matchTolerance, rebuildTrigger);
        CHECK_MSTATUS_AND_RETURN_IT(status);
        outputPoints = m_targetOrigPoints;
    }

    for (unsigned int v = 0; v < targetVertexCount; ++v)
    {
        const unsigned int start = m_offsets[v];
        const unsigned int end = m_offsets[v + 1];

        if (start == end)
        {
            // No source. Keep target orig position.
            continue;
        }

        if (end == start + 1)
        {
            const MapSource& src = m_sources[start];
            const auto it = sourcePointMap.find(src.sourceLogicalIndex);
            if (it != sourcePointMap.end() && it->second != nullptr && src.vertexIndex < it->second->length())
            {
                outputPoints.set((*it->second)[src.vertexIndex], v);
            }
            continue;
        }

        outputPoints.set(averageMappedSources(m_sources, sourcePointMap, start, end, outputPoints[v]), v);
    }

    MFnMeshData meshDataFn;
    MObject outputData = meshDataFn.create(&status);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    MFnMesh outputMeshFn;
    MObject outputMeshObj = outputMeshFn.copy(targetOrigMeshObj, outputData, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    outputMeshFn.setObject(outputMeshObj);
    status = outputMeshFn.setPoints(outputPoints, MSpace::kObject);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    MDataHandle outputHandle = dataBlock.outputValue(aOutputMesh, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    outputHandle.set(outputData);
    outputHandle.setClean();

    dataBlock.setClean(plug);
    return MS::kSuccess;
}
