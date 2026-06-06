#include "MeshAssemblerNode.h"

#include <maya/MArgDatabase.h>
#include <maya/MArgList.h>
#include <maya/MDagPath.h>
#include <maya/MFnDagNode.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MFnPlugin.h>
#include <maya/MGlobal.h>
#include <maya/MObject.h>
#include <maya/MPxCommand.h>
#include <maya/MPlug.h>
#include <maya/MSelectionList.h>
#include <maya/MStatus.h>
#include <maya/MString.h>
#include <maya/MStringArray.h>
#include <maya/MSyntax.h>

#include <sstream>
#include <string>
#include <vector>

namespace
{
constexpr double kDefaultMatchTolerance = 1.0e-4;
}

class CreateMeshAssemblerCmd : public MPxCommand
{
public:
    CreateMeshAssemblerCmd() = default;
    ~CreateMeshAssemblerCmd() override = default;

    MStatus doIt(const MArgList& args) override;
    bool isUndoable() const override { return false; }

    static void* creator() { return new CreateMeshAssemblerCmd(); }
    static MSyntax newSyntax();

    static const MString kCommandName;

private:
    static const char* kTargetShort;
    static const char* kTargetLong;
    static const char* kSourceShort;
    static const char* kSourceLong;
    static const char* kNameShort;
    static const char* kNameLong;
    static const char* kToleranceShort;
    static const char* kToleranceLong;

    static MString quoteMel(const MString& value)
    {
        std::string in(value.asChar());
        std::string out;
        out.reserve(in.size() + 2);
        out.push_back('"');
        for (char c : in)
        {
            if (c == '\\' || c == '"')
            {
                out.push_back('\\');
            }
            out.push_back(c);
        }
        out.push_back('"');
        return MString(out.c_str());
    }

    static MString quoteAttr(const MString& nodeName, const MString& attrName)
    {
        return quoteMel(nodeName + "." + attrName);
    }

    static MString dagLeafName(const MString& dagName)
    {
        std::string s(dagName.asChar());
        const std::size_t pipe = s.find_last_of('|');
        if (pipe != std::string::npos)
        {
            s = s.substr(pipe + 1);
        }
        const std::size_t colon = s.find_last_of(':');
        if (colon != std::string::npos)
        {
            s = s.substr(colon + 1);
        }
        if (s.empty())
        {
            s = "mesh";
        }
        return MString(s.c_str());
    }

    static bool isIntermediateMesh(const MObject& shapeObj)
    {
        MStatus status;
        MFnDependencyNode depFn(shapeObj, &status);
        if (status != MS::kSuccess)
        {
            return false;
        }

        MPlug intermediatePlug = depFn.findPlug("intermediateObject", true, &status);
        if (status != MS::kSuccess)
        {
            return false;
        }

        bool value = false;
        intermediatePlug.getValue(value);
        return value;
    }

    static MStatus findMeshShapeAndTransform(const MString& inputName,
                                                    MString& outShapeName,
                                                    MString& outTransformName)
    {
        MStatus status;
        MSelectionList sel;
        status = sel.add(inputName);
        if (status != MS::kSuccess)
        {
            MGlobal::displayError("createMeshAssembler: object not found: " + inputName);
            return status;
        }

        MDagPath path;
        status = sel.getDagPath(0, path);
        if (status != MS::kSuccess)
        {
            MGlobal::displayError("createMeshAssembler: object is not a DAG node: " + inputName);
            return status;
        }

        if (path.node().hasFn(MFn::kMesh))
        {
            outShapeName = path.fullPathName();
            MDagPath transformPath(path);
            status = transformPath.pop();
            CHECK_MSTATUS_AND_RETURN_IT(status);
            outTransformName = transformPath.fullPathName();
            return MS::kSuccess;
        }

        outTransformName = path.fullPathName();

        MFnDagNode dagFn(path, &status);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        for (unsigned int i = 0; i < dagFn.childCount(); ++i)
        {
            MObject child = dagFn.child(i, &status);
            if (status != MS::kSuccess || !child.hasFn(MFn::kMesh))
            {
                continue;
            }

            if (isIntermediateMesh(child))
            {
                continue;
            }

            MDagPath childPath(path);
            status = childPath.push(child);
            CHECK_MSTATUS_AND_RETURN_IT(status);
            outShapeName = childPath.fullPathName();
            return MS::kSuccess;
        }

        // Fallback: allow an intermediate mesh if there is no visible/non-intermediate mesh.
        for (unsigned int i = 0; i < dagFn.childCount(); ++i)
        {
            MObject child = dagFn.child(i, &status);
            if (status != MS::kSuccess || !child.hasFn(MFn::kMesh))
            {
                continue;
            }

            MDagPath childPath(path);
            status = childPath.push(child);
            CHECK_MSTATUS_AND_RETURN_IT(status);
            outShapeName = childPath.fullPathName();
            return MS::kSuccess;
        }

        MGlobal::displayError("createMeshAssembler: no mesh shape found under: " + inputName);
        return MS::kFailure;
    }

    static MStatus findMeshShape(const MString& inputName, MString& outShapeName)
    {
        MString transformName;
        return findMeshShapeAndTransform(inputName, outShapeName, transformName);
    }

    static MStatus getSelectionBasedInputs(MString& outTargetInput, std::vector<MString>& outSourceInputs)
    {
        MStatus status;
        MSelectionList selection;
        status = MGlobal::getActiveSelectionList(selection, true);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        const unsigned int selectionCount = selection.length();
        if (selectionCount < 2)
        {
            MGlobal::displayError("createMeshAssembler: select one or more source meshes, then select the target mesh last.");
            return MS::kFailure;
        }

        std::vector<MString> selectedDagNames;
        selectedDagNames.reserve(selectionCount);

        for (unsigned int i = 0; i < selectionCount; ++i)
        {
            MDagPath path;
            MObject component;
            status = selection.getDagPath(i, path, component);
            if (status != MS::kSuccess)
            {
                MGlobal::displayError("createMeshAssembler: selection contains a non-DAG item. Select mesh transforms or mesh shapes only.");
                return status;
            }

            selectedDagNames.push_back(path.fullPathName());
        }

        outTargetInput = selectedDagNames.back();
        outSourceInputs.assign(selectedDagNames.begin(), selectedDagNames.end() - 1);

        return MS::kSuccess;
    }

    static MStatus runMel(const MString& command)
    {
        return MGlobal::executeCommand(command, false, true);
    }

    static MStatus createOrigShapeSnapshot(const MString& inputName,
                                           MString& outOrigShape)
    {
        MStatus status;

        MString sourceShape;
        MString sourceTransform;
        status = findMeshShapeAndTransform(inputName, sourceShape, sourceTransform);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        const MString requestedShapeName = dagLeafName(sourceShape) + "Orig";
        const MString tmpName = "__meshAssemblerOrigTmp";
        const MString duplicateCommand = "duplicate -rr -name " + quoteMel(tmpName) + " " + quoteMel(sourceTransform);

        MStringArray duplicated;
        status = MGlobal::executeCommand(duplicateCommand, duplicated, false, true);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        if (duplicated.length() == 0)
        {
            MGlobal::displayError("createMeshAssembler: duplicate failed: " + inputName);
            return MS::kFailure;
        }

        const MString duplicateTransform = duplicated[0];

        // Bake the duplicated mesh as a static orig snapshot.
        status = runMel("delete -ch " + quoteMel(duplicateTransform));
        CHECK_MSTATUS_AND_RETURN_IT(status);

        MString duplicateShape;
        status = findMeshShape(duplicateTransform, duplicateShape);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        // Move the baked mesh shape under the original transform so orig and live
        // shapes share the same object space:
        //   source_meshShape -> source_meshShapeOrig
        MStringArray parentedShapes;
        const MString parentCommand = "parent -shape -relative " + quoteMel(duplicateShape) + " " + quoteMel(sourceTransform);
        status = MGlobal::executeCommand(parentCommand, parentedShapes, false, true);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        if (parentedShapes.length() == 0)
        {
            MGlobal::displayError("createMeshAssembler: failed to parent orig shape under: " + sourceTransform);
            return MS::kFailure;
        }

        MString renamedShape;
        status = MGlobal::executeCommand(
            "rename " + quoteMel(parentedShapes[0]) + " " + quoteMel(requestedShapeName),
            renamedShape,
            false,
            true);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        MStringArray longNames;
        status = MGlobal::executeCommand("ls -long " + quoteMel(renamedShape), longNames, false, true);
        CHECK_MSTATUS_AND_RETURN_IT(status);
        if (longNames.length() > 0)
        {
            outOrigShape = longNames[0];
        }
        else
        {
            outOrigShape = renamedShape;
        }

        status = runMel("setAttr " + quoteAttr(outOrigShape, "intermediateObject") + " 1");
        CHECK_MSTATUS_AND_RETURN_IT(status);

        // The duplicated transform is now just a temporary holder and can be removed.
        status = runMel("delete " + quoteMel(duplicateTransform));
        CHECK_MSTATUS_AND_RETURN_IT(status);

        return MS::kSuccess;
    }

};

const MString CreateMeshAssemblerCmd::kCommandName("createMeshAssembler");
const char* CreateMeshAssemblerCmd::kTargetShort = "-t";
const char* CreateMeshAssemblerCmd::kTargetLong = "-target";
const char* CreateMeshAssemblerCmd::kSourceShort = "-s";
const char* CreateMeshAssemblerCmd::kSourceLong = "-source";
const char* CreateMeshAssemblerCmd::kNameShort = "-n";
const char* CreateMeshAssemblerCmd::kNameLong = "-name";
const char* CreateMeshAssemblerCmd::kToleranceShort = "-tol";
const char* CreateMeshAssemblerCmd::kToleranceLong = "-tolerance";

MSyntax CreateMeshAssemblerCmd::newSyntax()
{
    MSyntax syntax;
    syntax.addFlag(kTargetShort, kTargetLong, MSyntax::kString);
    syntax.addFlag(kSourceShort, kSourceLong, MSyntax::kString);
    syntax.makeFlagMultiUse(kSourceShort);
    syntax.addFlag(kNameShort, kNameLong, MSyntax::kString);
    syntax.addFlag(kToleranceShort, kToleranceLong, MSyntax::kDouble);
    return syntax;
}

MStatus CreateMeshAssemblerCmd::doIt(const MArgList& args)
{
    MStatus status;
    MArgDatabase argDb(syntax(), args, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    const bool hasTargetFlag = argDb.isFlagSet(kTargetShort);
    const unsigned int sourceCount = argDb.numberOfFlagUses(kSourceShort);

    MString targetInput;
    std::vector<MString> sourceInputs;

    if (!hasTargetFlag && sourceCount == 0)
    {
        // Selection-order mode:
        //   selected[0..n-2] = sources
        //   selected[n-1]    = target
        status = getSelectionBasedInputs(targetInput, sourceInputs);
        CHECK_MSTATUS_AND_RETURN_IT(status);
    }
    else
    {
        if (!hasTargetFlag)
        {
            MGlobal::displayError("createMeshAssembler: -target/-t is required when command arguments are used.");
            return MS::kFailure;
        }

        if (sourceCount == 0)
        {
            MGlobal::displayError("createMeshAssembler: at least one -source/-s <source mesh> is required when command arguments are used.");
            return MS::kFailure;
        }

        status = argDb.getFlagArgument(kTargetShort, 0, targetInput);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        sourceInputs.reserve(sourceCount);
        for (unsigned int i = 0; i < sourceCount; ++i)
        {
            MString sourceInput;
            status = argDb.getFlagArgument(kSourceShort, i, sourceInput);
            CHECK_MSTATUS_AND_RETURN_IT(status);
            sourceInputs.push_back(sourceInput);
        }
    }

    MString targetShape;
    status = findMeshShape(targetInput, targetShape);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    MString requestedName;
    const bool hasRequestedName = argDb.isFlagSet(kNameShort);
    if (hasRequestedName)
    {
        status = argDb.getFlagArgument(kNameShort, 0, requestedName);
        CHECK_MSTATUS_AND_RETURN_IT(status);
    }

    double matchTolerance = kDefaultMatchTolerance;
    const bool hasMatchTolerance = argDb.isFlagSet(kToleranceShort);
    if (hasMatchTolerance)
    {
        status = argDb.getFlagArgument(kToleranceShort, 0, matchTolerance);
        CHECK_MSTATUS_AND_RETURN_IT(status);
    }

    MString targetOrigShape;
    status = createOrigShapeSnapshot(targetInput, targetOrigShape);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    MString createCommand = "createNode";
    if (hasRequestedName)
    {
        createCommand += " -name " + quoteMel(requestedName);
    }
    createCommand += " meshAssembler";

    MString nodeName;
    status = MGlobal::executeCommand(createCommand, nodeName, false, true);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    status = runMel("connectAttr -force " + quoteAttr(targetOrigShape, "outMesh") + " " + quoteAttr(nodeName, "targetOrigMesh"));
    CHECK_MSTATUS_AND_RETURN_IT(status);

    if (hasMatchTolerance)
    {
        std::ostringstream ss;
        ss.precision(17);
        ss << matchTolerance;
        status = runMel("setAttr " + quoteAttr(nodeName, "matchTolerance") + " " + MString(ss.str().c_str()));
        CHECK_MSTATUS_AND_RETURN_IT(status);
    }

    for (unsigned int i = 0; i < static_cast<unsigned int>(sourceInputs.size()); ++i)
    {
        const MString& sourceInput = sourceInputs[i];

        MString sourceShape;
        status = findMeshShape(sourceInput, sourceShape);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        MString sourceOrigShape;
        status = createOrigShapeSnapshot(sourceInput, sourceOrigShape);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        const MString indexStr(std::to_string(i).c_str());
        status = runMel("connectAttr -force " + quoteAttr(sourceOrigShape, "outMesh") + " " +
                         quoteMel(nodeName + ".source[" + indexStr + "].origMesh"));
        CHECK_MSTATUS_AND_RETURN_IT(status);

        status = runMel("connectAttr -force " + quoteAttr(sourceShape, "outMesh") + " " +
                         quoteMel(nodeName + ".source[" + indexStr + "].mesh"));
        CHECK_MSTATUS_AND_RETURN_IT(status);
    }

    status = runMel("connectAttr -force " + quoteAttr(nodeName, "outputMesh") + " " + quoteAttr(targetShape, "inMesh"));
    CHECK_MSTATUS_AND_RETURN_IT(status);

    MGlobal::displayInfo("createMeshAssembler: created " + nodeName +
                         " with orig shapes under source/target transforms. targetOrig=" + targetOrigShape);

    setResult(nodeName);
    return MS::kSuccess;
}


class RebuildMeshAssemblerCmd : public MPxCommand
{
public:
    RebuildMeshAssemblerCmd() = default;
    ~RebuildMeshAssemblerCmd() override = default;

    MStatus doIt(const MArgList& args) override
    {
        MStatus status;
        MArgDatabase argDb(syntax(), args, &status);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        if (!argDb.isFlagSet(kNodeShort))
        {
            MGlobal::displayError("rebuildMeshAssembler: -node/-n <meshAssembler node> is required.");
            return MS::kFailure;
        }

        MString nodeName;
        status = argDb.getFlagArgument(kNodeShort, 0, nodeName);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        const MString command = "setAttr " + quoteAttr(nodeName, "rebuildTrigger") +
                                " (`getAttr " + quoteAttr(nodeName, "rebuildTrigger") + "` + 1)";
        status = MGlobal::executeCommand(command, false, true);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        setResult(nodeName);
        return MS::kSuccess;
    }

    bool isUndoable() const override { return false; }

    static void* creator() { return new RebuildMeshAssemblerCmd(); }

    static MSyntax newSyntax()
    {
        MSyntax syntax;
        syntax.addFlag(kNodeShort, kNodeLong, MSyntax::kString);
        return syntax;
    }

    static const MString kCommandName;

private:
    static const char* kNodeShort;
    static const char* kNodeLong;

    static MString quoteMel(const MString& value)
    {
        std::string in(value.asChar());
        std::string out;
        out.reserve(in.size() + 2);
        out.push_back('"');
        for (char c : in)
        {
            if (c == '\\' || c == '"')
            {
                out.push_back('\\');
            }
            out.push_back(c);
        }
        out.push_back('"');
        return MString(out.c_str());
    }

    static MString quoteAttr(const MString& nodeName, const MString& attrName)
    {
        return quoteMel(nodeName + "." + attrName);
    }
};

const MString RebuildMeshAssemblerCmd::kCommandName("rebuildMeshAssembler");
const char* RebuildMeshAssemblerCmd::kNodeShort = "-n";
const char* RebuildMeshAssemblerCmd::kNodeLong = "-node";


MStatus initializePlugin(MObject obj)
{
    MStatus status;
    MFnPlugin plugin(obj, "local", "0.1.0", "Any", &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    status = plugin.registerNode(
        MeshAssemblerNode::kNodeName,
        MeshAssemblerNode::kNodeId,
        MeshAssemblerNode::creator,
        MeshAssemblerNode::initialize,
        MPxNode::kDependNode);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    status = plugin.registerCommand(
        CreateMeshAssemblerCmd::kCommandName,
        CreateMeshAssemblerCmd::creator,
        CreateMeshAssemblerCmd::newSyntax);
    CHECK_MSTATUS_AND_RETURN_IT(status);


    status = plugin.registerCommand(
        RebuildMeshAssemblerCmd::kCommandName,
        RebuildMeshAssemblerCmd::creator,
        RebuildMeshAssemblerCmd::newSyntax);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    return MS::kSuccess;
}

MStatus uninitializePlugin(MObject obj)
{
    MStatus status;
    MFnPlugin plugin(obj, "local", "0.1.0", "Any", &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    status = plugin.deregisterCommand(RebuildMeshAssemblerCmd::kCommandName);
    CHECK_MSTATUS_AND_RETURN_IT(status);


    status = plugin.deregisterCommand(CreateMeshAssemblerCmd::kCommandName);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    status = plugin.deregisterNode(MeshAssemblerNode::kNodeId);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    return MS::kSuccess;
}
