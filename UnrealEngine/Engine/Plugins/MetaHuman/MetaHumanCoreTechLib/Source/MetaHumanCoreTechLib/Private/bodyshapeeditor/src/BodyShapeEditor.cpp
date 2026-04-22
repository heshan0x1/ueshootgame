// Copyright Epic Games, Inc. All Rights Reserved.

#include <bodyshapeeditor/BodyShapeEditor.h>
#include <bodyshapeeditor/BodyMeasurement.h>
#include <bodyshapeeditor/SerializationHelper.h>
#include <bodyshapeeditor/BodyJointEstimator.h>
#include <rig/RigLogic.h>
#include <rig/TwistSwingLogic.h>
#include <trio/Stream.h>
#include <Eigen/src/Core/Map.h>
#include <arrayview/ArrayView.h>
#include <carbon/Algorithm.h>
#include <carbon/common/Log.h>
#include <carbon/utils/ObjectPool.h>
#include <carbon/utils/StringReplace.h>
#include <carbon/utils/StringUtils.h>
#include <carbon/utils/Timer.h>
#include <regex>
#include <terse/archives/binary/InputArchive.h>
#include <terse/archives/binary/OutputArchive.h>
#include <rig/BodyGeometry.h>
#include <rig/BodyLogic.h>
#include <rig/RBFLogic.h>
#include <rig/SymmetricControls.h>
#include <rig/SkinningWeightUtils.h>
#include <carbon/common/Defs.h>
#include <nls/Context.h>
#include <nls/Cost.h>
#include <nrr/deformation_models/DeformationModelVertex.h>
#include <nls/geometry/EulerAngles.h>
#include <nls/geometry/Procrustes.h>
#include <nls/geometry/Quaternion.h>
#include <nls/BoundedVectorVariable.h>
#include <nls/solver/GaussNewtonSolver.h>
#include <nls/DiffData.h>
#include <nls/math/Math.h>
#include <nls/math/ParallelBLAS.h>
#include <nls/VectorVariable.h>
#include <nls/functions/LimitConstraintFunction.h>
#include <nls/functions/ProjectionConstraintFunction.h>
#include <nls/functions/AxisConstraintFunction.h>
#include <nls/functions/LengthConstraintFunction.h>
#include <nls/functions/SubtractFunction.h>
#include <nls/functions/PointPointConstraintFunction.h>
#include <nls/geometry/Mesh.h>
#include <nls/solver/SimpleGaussNewtonSolver.h>

#include <Eigen/src/Core/Matrix.h>
#include <carbon/io/JsonIO.h>


#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

CARBON_NAMESPACE_BEGIN(TITAN_NAMESPACE)

namespace
{

struct SparseMatrixPCA
{
    SparseMatrix<float> mean;
    std::vector<Eigen::MatrixXf> mods;
    std::vector<std::vector<int>> rowsPerPart;
    std::vector<std::vector<int>> colIndicesPerRow;
    Eigen::MatrixXf globalToMods;


    int pcaModCount()
    {
        int i = 0;
        for (const auto& m : mods)
        {
            i += m.cols();
        }
        return i;
    }

    int numColsForRows(const std::vector<int>& rows)
    {
        int totalCols = 0;
        for (int ri : rows)
        {
            totalCols += static_cast<int>(colIndicesPerRow[ri].size());
        }
        return totalCols;
    }

    void ReadFromDNA(const dna::Reader* reader, std::string model_name)
    {
        auto modelStr64 = reader->getMetaDataValue(model_name.c_str());
        if (modelStr64.size() == 0u)
        {
            return;
        }
        auto modelStr = TITAN_NAMESPACE::Base64Decode(std::string { modelStr64.begin(), modelStr64.end() });
        auto stream = pma::makeScoped<trio::MemoryStream>(modelStr.size());
        stream->open();
        stream->write(modelStr.data(), modelStr.size());
        stream->seek(0);
        modelStr.clear();
        terse::BinaryInputArchive<trio::MemoryStream> archive { stream.get() };

        std::vector<std::uint32_t> rows;
        std::vector<std::uint32_t> cols;
        std::vector<float> values;

        SparseMatrix<float>::Index colCount;
        SparseMatrix<float>::Index rowCount;
        archive(colCount);
        archive(rowCount);
        archive(rows);
        archive(cols);
        archive(values);
        CARBON_ASSERT(rows.size() == cols.size(), "Model matrix has wrong entries");
        CARBON_ASSERT(rows.size() == values.size(), "Model matrix has wrong entries");
        std::vector<Eigen::Triplet<float>> triplets;
        triplets.reserve(values.size());
        for (size_t j = 0; j < rows.size(); j++)
        {
            triplets.push_back(Eigen::Triplet<float>(rows[j], cols[j], values[j]));
        }
        mean.resize(rowCount, colCount);
        mean.setFromTriplets(triplets.begin(), triplets.end());

        auto archiveDynMatrix = [&](Eigen::MatrixXf& matrix)
        {
            Eigen::MatrixXf::Index colCount;
            Eigen::MatrixXf::Index rowCount;
            archive(colCount);
            archive(rowCount);
            std::vector<float> values;
            archive(values);
            matrix = Eigen::Map<Eigen::MatrixXf> { values.data(), rowCount, colCount };
        };

        decltype(mods)::size_type modCount;
        archive(modCount);

        mods.resize(modCount);
        for (std::size_t mi = 0u; mi < modCount; ++mi)
        {
            archiveDynMatrix(mods[mi]);
        }

        archive(rowsPerPart);
        archive(colIndicesPerRow);
        archiveDynMatrix(globalToMods);
    }

    void WriteToStream(trio::BoundedIOStream* stream)
    {
        terse::BinaryOutputArchive<trio::BoundedIOStream> archive { stream };
        std::string serializationVersion = ("0.0.2");
        archive(serializationVersion);
        std::vector<std::uint32_t> rows;
        std::vector<std::uint32_t> cols;
        std::vector<float> values;
        for (int k = 0; k < mean.outerSize(); ++k)
        {
            for (SparseMatrix<float>::InnerIterator it(mean, k); it; ++it)
            {
                rows.push_back(it.row());
                cols.push_back(it.col());
                values.push_back(it.value());
            }
        }
        archive(mean.cols());
        archive(mean.rows());
        archive(rows);
        archive(cols);
        archive(values);
        archive(mods.size());
        for (const auto& mod : mods)
        {
            archive(mod.cols());
            archive(mod.rows());
            archive(std::vector<float>(mod.data(), mod.data() + mod.size()));
        }

        archive(rowsPerPart);
        archive(colIndicesPerRow);
        archive(globalToMods.cols());
        archive(globalToMods.rows());
        archive(std::vector<float>(globalToMods.data(), globalToMods.data() + globalToMods.size()));
    }

    void ReadFromStream(trio::BoundedIOStream* stream)
    {
        terse::BinaryInputArchive<trio::BoundedIOStream> archive { stream };
        std::string serializationVersion;
        archive(serializationVersion);

        if (serializationVersion != "0.0.2")
        {
            CARBON_CRITICAL("Serialization version mismatch: expected 0.0.2, got {}", serializationVersion.c_str());
        }

        std::vector<std::uint32_t> rows;
        std::vector<std::uint32_t> cols;
        std::vector<float> values;

        SparseMatrix<float>::Index colCount;
        SparseMatrix<float>::Index rowCount;
        archive(colCount);
        archive(rowCount);
        archive(rows);
        archive(cols);
        archive(values);
        CARBON_ASSERT(rows.size() == cols.size(), "Model matrix has wrong entries");
        std::vector<Eigen::Triplet<float>> triplets;
        triplets.reserve(values.size());
        CARBON_ASSERT(rows.size() == values.size(), "Model matrix has wrong entries");
        for (size_t j = 0; j < rows.size(); j++)
        {
            triplets.push_back(Eigen::Triplet<float>(rows[j], cols[j], values[j]));
        }
        {
            mean.resize(rowCount, colCount);
            mean.setFromTriplets(triplets.begin(), triplets.end());
        }

        auto archiveDynMatrix = [&](Eigen::MatrixXf& matrix)
        {
            Eigen::MatrixXf::Index colCount;
            Eigen::MatrixXf::Index rowCount;
            archive(colCount);
            archive(rowCount);
            std::vector<float> values;
            archive(values);
            matrix = Eigen::Map<Eigen::MatrixXf> { values.data(), rowCount, colCount };
        };

        decltype(mods)::size_type modCount;
        archive(modCount);

        mods.resize(modCount);
        for (std::size_t mi = 0u; mi < modCount; ++mi)
        {
            archiveDynMatrix(mods[mi]);
        }

        archive(rowsPerPart);
        archive(colIndicesPerRow);
        archiveDynMatrix(globalToMods);
    }

    SparseMatrix<float> calculateResult(const Eigen::VectorXf& global)
    {
        auto pcaCoeffAllRegions = globalToMods * global;
        std::size_t inputOffset = 0;
        auto result = mean;
        for (int ri = 0; ri < mods.size(); ++ri)
        {
            const auto& mod = mods[ri];
            auto pcaCoeff = pcaCoeffAllRegions.middleRows(inputOffset, mod.cols()).transpose().array();
            Eigen::VectorXf regionResult = mod.col(0) * pcaCoeff[0];

            for (int mi = 1; mi < mod.cols(); ++mi)
            {
                regionResult += mod.col(mi) * pcaCoeff[mi];
            }

            inputOffset += mod.cols();

            int jOffset = 0;
            for (int i = 0; i < rowsPerPart[ri].size(); ++i)
            {
                const auto rowIndex = rowsPerPart[ri][i];
                for (std::uint16_t j = 0; j < colIndicesPerRow[rowIndex].size(); ++j)
                {
                    const auto ji = colIndicesPerRow[rowIndex][j];
                    result.coeffRef(rowIndex, ji) += regionResult[jOffset];
                    jOffset++;
                }
            }
        }
        return result;
    }
};

template <typename T>
std::shared_ptr<BodyLogic<T>> PoseLogicFromString(const std::vector<std::string>& lines,
    const std::shared_ptr<BodyLogic<T>>& rl,
    const std::shared_ptr<BodyGeometry<T>>& bg,
    const std::vector<int>& coreJoints,
    const std::string controlNamePrefix = "pose_")
{
    // create a copy of the input logic
    std::shared_ptr<BodyLogic<T>> logic = rl->Clone();

    // dof lookup table
    static const std::array<std::string, 6> dofNames = { "tx", "ty", "tz", "rx", "ry", "rz" };
    auto getDofIndex = [&](const std::string& name)
    {
        for (int id = 0; id < 6; id++)
        {
            if (dofNames[id] == name)
            {
                return id;
            }
        }
        return -1;
    };

    // regex splitting line up into [parameter name] [limit min] [limit max] [limit weight] [joint group]
    std::regex parameterEx("(\\S*)\\s*\\[\\s*([+-]?[0-9]*[.]?[0-9]+)\\s*,\\s*([+-]?[0-9]*[.]?[0-9]+),\\s*([+-]?[0-9]*[.]?[0-9]+)\\s*\\]\\s*->\\s*(.*)");
    std::regex jointEx("\\s*([+-]?[0-9]*[.]?[0-9]+)\\s*\\*\\s*(\\w*).([tr][xyz])\\s*,*");

    // go through all lines of the string
    std::smatch res;

    std::vector<Eigen::Triplet<T>> jointTriplets;

    SparseMatrix<T> jointMatrix = logic->GetJointMatrix(0);
    auto replaceAllLR = [](std::string s, char side)
    {
        const std::string from = "_LR";
        const std::string to(1, side); 
        size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::string::npos)
        {
            s.replace(pos + 1, from.length() - 1, to);
            pos += to.length();
        }
        return s;
    };
    const auto parseLine = [&](const std::string& line)
    {
        if (std::regex_search(line, res, parameterEx) && (res.size() == 6))
        {

            std::string controlName = res[1].str();
            std::string prefix = controlNamePrefix + "anatomical_";
            if (controlName.starts_with("pelvis") || controlName.starts_with("root"))
            {
                prefix = controlNamePrefix + "rigid_";
            }
            std::string name = prefix + res[1].str();
            const T limitMin = static_cast<T>(std::stod(res[2]));
            const T limitMax = static_cast<T>(std::stod(res[3]));

            const int guiIndex = logic->NumGUIControls();
            const int rawIndex = logic->NumRawControls();
            logic->GuiControlNames().push_back(name);
            logic->RawControlNames().push_back(name);
            logic->GuiToRawMapping().push_back({ guiIndex, rawIndex, limitMin, limitMax, 1.0f, 0.0f });
            logic->GuiControlRanges().conservativeResize(2, logic->GuiControlRanges().cols() + 1);
            logic->GuiControlRanges()(0, guiIndex) = limitMin;
            logic->GuiControlRanges()(1, guiIndex) = limitMax;

            jointMatrix.conservativeResize(jointMatrix.rows(), jointMatrix.cols() + 1);

            const std::string jointData = res[5];
            for (std::sregex_iterator i = std::sregex_iterator(jointData.begin(), jointData.end(), jointEx); i != std::sregex_iterator(); i++)
            {
                std::smatch jmap = *i;
                if (jmap.size() == 4)
                {
                    const int jointId = bg->GetJointIndex(jmap[2]);
                    const int dofId = getDofIndex(jmap[3]);
                    const T weight = static_cast<T>(std::stod(jmap[1]));

                    // check for invalid joint names
                    if ((jointId < 0) || (dofId < 0))
                    {
                        continue;
                    }

                    // add entry to the joint matrix
                    jointMatrix.coeffRef(jointId * 9 + dofId, rawIndex) = weight;
                }
            }
        }
    };


    for (const auto& line : lines)
    {
        if (line.empty() || (line[0] == '#'))
        {
            continue;
        }

        if (line.find("_LR") != std::string::npos)
        {
            parseLine(replaceAllLR(line, 'l'));
            parseLine(replaceAllLR(line, 'r'));
        } else 
        {
            parseLine(line);
        }
    }
    for (int ji : coreJoints)
    {
        if (ji < 2)
        {
            // we want to skip pelvis and root
            continue;
        }
        int i = 0;
        const std::string jointName = bg->GetJointNames()[ji];
        T limitMin = static_cast<T>(-CARBON_PI / 8.0);
        T limitMax = static_cast<T>(CARBON_PI / 8.0);
        if(jointName.find("toe_", 0) != std::string::npos)
        {
            continue;
        }
        for (const auto& prefix : { "pinky_0", "ring_0", "index_0", "thumb_0", "middle_0" })
        {
            if (jointName.starts_with(prefix))
            {
                limitMin = static_cast<T>(-0.1);
                limitMax = static_cast<T>(1.2);
                i = 2;
                break;
            }
        }

        for (; i < 3; ++i)
        {
            const int guiIndex = logic->NumGUIControls();
            const int rawIndex = logic->NumRawControls();
            std::string name = controlNamePrefix + "driver_" + bg->GetJointNames()[ji] + "." + dofNames[3 + i];
            logic->GuiControlNames().push_back(name);
            logic->RawControlNames().push_back(name);
            logic->GuiToRawMapping().push_back({ guiIndex, rawIndex, limitMin, limitMax, 1.0f, 0.0f });
            logic->GuiControlRanges().conservativeResize(2, logic->GuiControlRanges().cols() + 1);
            logic->GuiControlRanges()(0, guiIndex) = limitMin;
            logic->GuiControlRanges()(1, guiIndex) = limitMax;
            jointMatrix.conservativeResize(jointMatrix.rows(), jointMatrix.cols() + 1);
            jointMatrix.coeffRef(ji * 9 + 3 + i, rawIndex) = 1.0f;
        }
    }

    logic->GetJointMatrix(0) = jointMatrix;

    return logic;
}

} // namespace

struct BodyShapeEditor::State::Private
{
    Eigen::VectorXf RawControls;
    Eigen::Matrix<float, 3, -1> VertexDeltas;
    Eigen::Matrix<float, 3, -1> JointDeltas;
    Eigen::VectorXf CustomPose;
    float VertexDeltaScale { 1.0f };

    Eigen::VectorXf GuiControls;
    std::vector<Mesh<float>> RigMeshes;
    std::vector<Eigen::Transform<float, 3, Eigen::Affine>> JointBindMatrices;
    std::vector<BodyMeasurement> Constraints;
    //! evaluated measurements of the current state
    Eigen::VectorXf ConstraintMeasurements;
    //! user specified target measurements
    std::vector<std::pair<int, float>> TargetMeasurements;
    bool UseSymmetry = true;
    float SemanticWeight = 10.0f;
    bool FloorOffsetApplied = true;
    std::string ModelVersion;

    // tmp
    //! gui controls prior (e.g. from blending or from template2MH)
    Eigen::VectorXf GuiControlsPrior;
    SparseMatrix<float> CustomSkinning;
};

BodyShapeEditor::State::State()
    : m { new Private() }
{
}

BodyShapeEditor::State::~State()
{
}

BodyShapeEditor::State::State(const State& other)
    : m(new Private(*other.m))
{
}

void BodyShapeEditor::State::SetSymmetry(const bool sym) { m->UseSymmetry = sym; }
bool BodyShapeEditor::State::GetSymmetric() const { return m->UseSymmetry; }
float BodyShapeEditor::State::GetSemanticWeight() { return m->SemanticWeight; }
void BodyShapeEditor::State::SetSemanticWeight(float weight) { m->SemanticWeight = weight; }
bool BodyShapeEditor::State::GetApplyFloorOffset() const { return m->FloorOffsetApplied; }
void BodyShapeEditor::State::SetApplyFloorOffset(bool floorOffset) { m->FloorOffsetApplied = floorOffset; }
void BodyShapeEditor::State::SetVertexInfluenceWeights(const SparseMatrix<float>& vertexInfluenceWeights) {m->CustomSkinning = vertexInfluenceWeights;}
float BodyShapeEditor::State::VertexDeltaScale() const { return m->VertexDeltaScale; }
void BodyShapeEditor::State::SetVertexDeltaScale(float VertexDeltaScale) { m->VertexDeltaScale = VertexDeltaScale; }

const Eigen::VectorX<float>& BodyShapeEditor::State::GetPCACoeff() const
{
    return m->GuiControls;
}

const Eigen::VectorX<float>& BodyShapeEditor::State::GetCustomPose() const 
{
    return m->CustomPose;
}

const Mesh<float>& BodyShapeEditor::State::GetMesh(int lod) const
{
    return m->RigMeshes[static_cast<size_t>(lod)];
}

const std::vector<Eigen::Transform<float, 3, Eigen::Affine>>& BodyShapeEditor::State::GetJointBindMatrices() const
{
    return m->JointBindMatrices;
}

const Eigen::VectorXf& BodyShapeEditor::State::GetNamedConstraintMeasurements() const
{
    if (m->ConstraintMeasurements.size() == 0)
    {
        m->ConstraintMeasurements = BodyMeasurement::GetBodyMeasurements(m->Constraints, m->RigMeshes[0].Vertices(), m->RawControls);
    }
    return m->ConstraintMeasurements;
}

Eigen::Matrix3Xf BodyShapeEditor::State::GetContourVertices(int ConstraintIndex) const
{
    if (m->Constraints.size() <= ConstraintIndex)
    {
        CARBON_CRITICAL("Invalid ConstraintIndex");
    }
    return m->Constraints[ConstraintIndex].GetMeasurementPoints();
}

Eigen::Matrix3Xf BodyShapeEditor::State::GetContourDebugVertices(int ConstraintIndex) const
{
    if (m->Constraints.size() <= ConstraintIndex)
    {
        CARBON_CRITICAL("Invalid ConstraintIndex");
    }
    return m->Constraints[ConstraintIndex].GetMeasurementDebugPoints(m->RigMeshes[0].Vertices());
}

void BodyShapeEditor::State::Reset()
{
    m->RawControls.setZero();
    m->VertexDeltas.setZero();
    m->GuiControls.setZero();
    m->TargetMeasurements.clear();
    m->VertexDeltaScale = 1.0f;
    m->GuiControlsPrior.setZero();
}

int BodyShapeEditor::State::GetConstraintNum() const
{
    return static_cast<int>(m->Constraints.size());
}

const std::string& BodyShapeEditor::State::GetConstraintName(int ConstraintIndex) const
{
    if (m->Constraints.size() <= ConstraintIndex)
    {
        CARBON_CRITICAL("Invalid ConstraintIndex");
    }
    return m->Constraints[ConstraintIndex].GetName();
}

bool BodyShapeEditor::State::GetConstraintTarget(int ConstraintIndex, float& OutTarget) const
{
    auto it = std::find_if(m->TargetMeasurements.begin(), m->TargetMeasurements.end(),
        [ConstraintIndex](const std::pair<int, float>& el)
        {
            return el.first == ConstraintIndex;
        });

    if (it != m->TargetMeasurements.end())
    {
        OutTarget = it->second;
        return true;
    }

    return false;
}

void BodyShapeEditor::State::SetConstraintTarget(int ConstraintIndex, float Target)
{
    if (m->Constraints.size() <= ConstraintIndex)
    {
        CARBON_CRITICAL("Invalid ConstraintIndex");
    }
    std::pair<int, float> TargetMeasurement { ConstraintIndex, Target };
    auto it = std::lower_bound(m->TargetMeasurements.begin(), m->TargetMeasurements.end(), TargetMeasurement,
        [](const std::pair<int, float>& elA, const std::pair<int, float>& elB)
        {
            return elA.first < elB.first;
        });

    if (it != m->TargetMeasurements.end())
    {
        if (it->first == ConstraintIndex)
        {
            it->second = Target;
            return;
        }
    }
    m->TargetMeasurements.insert(it, TargetMeasurement);
}

void BodyShapeEditor::State::RemoveConstraintTarget(int ConstraintIndex)
{
    if (m->Constraints.size() <= ConstraintIndex)
    {
        CARBON_CRITICAL("Invalid ConstraintIndex");
    }
    auto it = std::find_if(m->TargetMeasurements.begin(), m->TargetMeasurements.end(),
        [ConstraintIndex](const std::pair<int, float>& el)
        {
            return el.first == ConstraintIndex;
        });

    if (it != m->TargetMeasurements.end())
    {
        m->TargetMeasurements.erase(it);
    }
}

struct BodyShapeEditor::Private
{
    std::unique_ptr<SymmetricControls<float>> symControls;
    std::shared_ptr<BodyLogic<float>> rigLogic;
    std::shared_ptr<BodyLogic<float>> poseLogic;
    std::shared_ptr<RBFLogic<float>> rbfLogic;
    std::shared_ptr<TwistSwingLogic<float>> twistSwingLogic;
    std::shared_ptr<BodyGeometry<float>> rigGeometry;
    std::shared_ptr<BodyGeometry<float>> combinedBodyArchetypeRigGeometry;
    std::vector<Eigen::VectorX<std::uint16_t>> jointGroupInputIndices;
    std::vector<Eigen::VectorX<std::uint16_t>> jointGroupOutputIndices;
    std::string ModelVersion;
    std::vector<BodyMeasurement> Constraints;
    std::vector<std::pair<std::vector<std::string>, std::vector<std::string>>> solveSteps;
    std::vector<int> localIndices;
    std::vector<int> globalIndices;
    std::vector<int> poseIndices;
    std::vector<int> rawLocalIndices;
    std::vector<int> rawPoseIndices;
    std::vector<std::vector<int>> bodyToCombinedMapping;
    std::vector<std::map<int, int>> combinedToBodyMapping;
    std::shared_ptr<const LodGeneration<float>> combinedLodGenerationData;
    std::vector<Eigen::Matrix<int, 3, -1>> meshTriangles;
    SparseMatrix<float> gwm;
    ObjectPool<BodyGeometry<float>::State> StatePool;
    ObjectPool<BodyGeometry<float>::State> StatePoolJacobian;
    std::shared_ptr<Mesh<float>> triTopology;
    std::shared_ptr<HalfEdgeMesh<float>> heTopology;
    std::shared_ptr<TaskThreadPool> threadPool;
    std::vector<float> minMeasurementInput;
    std::vector<float> maxMeasurementInput;
	std::vector<std::pair<float, float>> variableMinMeasurementInput;
	std::vector<std::pair<float, float>> variableMaxMeasurementInput;
	int heightConstraintIndex = -1;

    std::vector<int> combinedFittingIndices;
    std::vector<std::vector<int>> neckSeamIndices;
    std::vector<int> keypoints;

    std::map<std::string, SparseMatrixPCA> skinningModels; 

    SparseMatrixPCA rbfPCA;
    SparseMatrixPCA skinWeightsPCA;
    //! region names
    std::vector<std::string> regionNames;

    //! map of skeleton pca region to affectedJoints
    std::map<std::string, std::set<int>> regionToJoints;
    //! map of skeleton pca region to raw controls
    std::map<std::string, std::vector<int>> skeletonPcaControls;
    //! map of shape pca region to raw controls
    std::map<std::string, std::vector<int>> shapePcaControls;
    //! symmetric mapping of pca regions
    std::map<std::string, std::string> symmetricPartMapping;
    //! mapping from raw to gui controls
    std::vector<int> rawToGuiControls;
    //! mapping from gui to raw controls
    std::vector<int> guiToRawControls;
    //! linear matrix mapping gui to raw controls: rawControls = guiToRawMapping * guiControls
    Eigen::SparseMatrix<float, Eigen::RowMajor> guiToRawMappingMatrix;
    // Eigen::MatrixXf guiToRawMappingMatrix;
    //! matrix to solve from raw to global gui controls
    Eigen::MatrixX<float> rawToGlobalGuiControlsSolveMatrix;
    //! vertex mask for each pca part
    std::map<std::string, VertexWeights<float>> partWeights;

    //! identity vertex evaluation matrix from raw controls
    Eigen::SparseMatrix<float, Eigen::RowMajor> identityVertexEvaluationMatrix;
    //! identity joint evlauation matrix from raw controls
    Eigen::SparseMatrix<float, Eigen::RowMajor> identityJointEvaluationMatrix;
    //! identity vertex evaluation matrix from symmetric controls
    Eigen::SparseMatrix<float, Eigen::RowMajor> symmetricIdentityVertexEvaluationMatrix;

    BodyJointEstimator jointEstimator;

    int floorIndex { -1 };
    int rbfControlOffset { 0 };

    void CalculateCombinedLods(BodyShapeEditor::State& state) const;
    std::vector<int> maxSkinWeights = { 12, 8, 8, 4 };
    std::vector<std::map<std::string, std::map<std::string, float>>> jointSkinningWeightLodPropagationMap;
    std::vector<SnapConfig<float>> skinningWeightSnapConfigs;

    //! calculate the skinning weight snap config for the specified lod
    SnapConfig<float> CalcNeckSeamSkinningWeightsSnapConfig(int lod) const;

    public:
        static constexpr int32_t MagicNumber = 0x8c3b5f5e;
	void SetMinMaxMeasurements(const State& State);
};

BodyShapeEditor::~BodyShapeEditor()
{
    delete m;
}

BodyShapeEditor::BodyShapeEditor()
    : m { new Private() }
{
}

int BodyShapeEditor::GetNumLOD0MeshVertices(bool bInCombined) const
{
	if (bInCombined)
	{
		return m->rigGeometry->GetMesh(0).NumVertices();
	}
	else
	{
		return static_cast<int>(m->bodyToCombinedMapping[0].size());
	}
}

const std::vector<int>& BodyShapeEditor::GetMaxSkinWeights() const
{
    return m->maxSkinWeights;
}

void BodyShapeEditor::SetMaxSkinWeights(const std::vector<int>& maxSkinWeights) { m->maxSkinWeights = maxSkinWeights; }


void BodyShapeEditor::SetThreadPool(const std::shared_ptr<TaskThreadPool>& threadPool) { m->threadPool = threadPool; }

void BodyShapeEditor::Private::CalculateCombinedLods(BodyShapeEditor::State& state) const
{
    if (combinedLodGenerationData)
    {
        std::map<std::string, Eigen::Matrix<float, 3, -1>> lod0Vertices, higherLodVertices;
        const auto baseMeshes = combinedLodGenerationData->Lod0MeshNames();
        if (baseMeshes.size() != 1)
        {
            CARBON_CRITICAL("There should be 1 lod 0 mesh for the combined body model");
        }
        lod0Vertices[baseMeshes[0]] = state.m->RigMeshes[0].Vertices();

        bool bCalculatedLods = combinedLodGenerationData->Apply(lod0Vertices, higherLodVertices);
        if (!bCalculatedLods)
        {
            CARBON_CRITICAL("Failed to generate lods for the combined body model");
        }
        for (const auto& lodVertices : higherLodVertices)
        {
            int lod = combinedLodGenerationData->LodForMesh(lodVertices.first);
            state.m->RigMeshes[static_cast<size_t>(lod)].SetVertices(lodVertices.second);
            state.m->RigMeshes[static_cast<size_t>(lod)].CalculateVertexNormals(true, VertexNormalComputationType::AreaWeighted, /*stableNormalize=*/true);
        }
    }
}

std::shared_ptr<BodyShapeEditor::State> BodyShapeEditor::CreateState() const
{
    auto state = std::shared_ptr<State>(new State());
    state->m->GuiControls = Eigen::VectorX<float>::Zero(m->rigLogic->NumGUIControls());
    state->m->Constraints = m->Constraints;
    state->m->JointBindMatrices = m->rigGeometry->GetBindMatrices();
    state->m->JointDeltas = Eigen::Matrix<float, 3, -1>::Zero(3,m->rigGeometry->NumJoints());
    state->m->VertexDeltas = Eigen::Matrix<float,3 , -1>::Zero(3, m->rigGeometry->GetMesh(0).Vertices().cols());
    state->m->CustomPose = Eigen::VectorXf::Zero(m->rigGeometry->NumJoints() * 9u);
    state->m->ModelVersion = m->ModelVersion;
    EvaluateState(*state);
    return state;
}

void BodyShapeEditor::EvaluateState(State& State) const
{
    State.m->CustomSkinning.resize(0, 0);
    State.m->RawControls = m->rigLogic->EvaluateRawControls(State.m->GuiControls).Value();
    Eigen::Matrix3Xf vertices;
    if (State.m->RawControls(m->rawPoseIndices).squaredNorm() > 0)
    {
        // evaluate using riglogic when poses are activated
        BodyGeometry<float>::State geometryState;
        const DiffData<float> joints = m->rigLogic->EvaluateJoints(0, State.m->RawControls);
        if (State.m->VertexDeltas.size() > 0)
        {
            m->rigGeometry->EvaluateBodyGeometryWithOffset(0,
                State.m->VertexDeltaScale * State.m->VertexDeltas,
                joints,
                State.m->RawControls,
                geometryState);
        }
        else
        {
            m->rigGeometry->EvaluateBodyGeometry(0, joints, State.m->RawControls, geometryState);
        }
        vertices = geometryState.Vertices().Matrix();
    }
    else
    {
        // use linear matrix for activation
        const int numVertices = m->rigGeometry->GetMesh(0).NumVertices();
        Eigen::VectorXf rawLocalControls = State.m->RawControls(m->rawLocalIndices);
        if (m->threadPool)
        {
            vertices.resize(3, numVertices);
            ParallelNoAliasGEMV<float>(vertices.reshaped(), m->identityVertexEvaluationMatrix, rawLocalControls, m->threadPool.get());
            if (((int)State.m->VertexDeltas.cols() == m->rigGeometry->GetMesh(0).NumVertices()) && (State.m->VertexDeltaScale > 0))
            {
                vertices += m->rigGeometry->GetMesh(0).Vertices() + State.m->VertexDeltaScale * State.m->VertexDeltas;
            }
            else
            {
                vertices += m->rigGeometry->GetMesh(0).Vertices();
            }
        }
        else
        {
            vertices = (m->identityVertexEvaluationMatrix * rawLocalControls + m->rigGeometry->GetMesh(0).Vertices().reshaped()).reshaped(3, numVertices);
            if (((int)State.m->VertexDeltas.cols() == m->rigGeometry->GetMesh(0).NumVertices()) && (State.m->VertexDeltaScale > 0))
            {
                vertices += State.m->VertexDeltaScale * State.m->VertexDeltas;
            }
        }
    }
    Eigen::Vector3f ModelTranslation = State.m->CustomPose({0,1,2});
    vertices.colwise() += ModelTranslation;
    Eigen::VectorXf jointDeltas = m->identityJointEvaluationMatrix *  State.m->RawControls(m->rawLocalIndices);
    
    for(int ji : m->jointEstimator.CoreJoints())
    {
        State.m->JointBindMatrices[ji].translation() = jointDeltas.segment(3 * ji, 3) + m->rigGeometry->GetBindMatrices()[ji].translation() + ModelTranslation;
        if (State.m->JointDeltas.cols() > 0)
        {
            State.m->JointBindMatrices[ji].translation() += State.m->VertexDeltaScale * State.m->JointDeltas.col(ji);
        }
    }
    Eigen::Matrix<float, 3, -1> jointBindPose;
    jointBindPose.resize(3, State.m->JointBindMatrices.size());
    for(int ji = 0; ji < jointBindPose.cols(); ji++)
    {
        jointBindPose.col(ji) = State.m->JointBindMatrices[ji].translation();
    }

    const auto& vjm = m->jointEstimator.VertexJointMatrix();
    const auto& jjm = m->jointEstimator.JointJointMatrix();
    const auto dependentJoints = (jointBindPose * jjm).eval();
    for(int ji : m->jointEstimator.DependentJoints())
    {
        State.m->JointBindMatrices[ji].translation() = dependentJoints.col(ji);
    }
    const auto surfaceJoints = (vertices * vjm).eval();
    for(int ji : m->jointEstimator.SurfaceJoints())
    {
        State.m->JointBindMatrices[ji].translation() = surfaceJoints.col(ji) + State.m->VertexDeltaScale * State.m->JointDeltas.col(ji);
    }

    if (State.m->FloorOffsetApplied)
    {
        // get floor position (using index or lowest vertex in the mesh) and move vertices and joints
        float floorOffset = 0;
        if (m->floorIndex >= 0)
        {
            floorOffset = vertices.row(1)[m->floorIndex];
        }
        else
        {
            floorOffset = vertices.row(1).minCoeff();
        }
        vertices.row(1).array() -= floorOffset;

        Eigen::Vector3f offsetTranslation(0.0f, floorOffset, 0.0f);
        for (int i = 1; i < (int)State.m->JointBindMatrices.size(); i++)
        {
            State.m->JointBindMatrices[i].translation() -= offsetTranslation;
        }
    }
    State.m->JointBindMatrices[0].translation() = Eigen::Vector3f::Zero();
    // make sure the rig meshes have the right triangulation
    State.m->RigMeshes.resize(m->meshTriangles.size());
    for (size_t i = 0; i < m->meshTriangles.size(); ++i)
    {
        if (State.m->RigMeshes[i].NumTriangles() != (int)m->meshTriangles[i].cols())
        {
            State.m->RigMeshes[i].SetTriangles(m->meshTriangles[i]);
        }
    }
    // update LOD0
    State.m->RigMeshes[0].SetVertices(vertices);
    State.m->RigMeshes[0].CalculateVertexNormals(true, VertexNormalComputationType::AreaWeighted, /*stableNormalize=*/true, m->threadPool.get());

    // update other LODs
    m->CalculateCombinedLods(State);
    BodyMeasurement::UpdateBodyMeasurementPoints(State.m->Constraints, vertices, State.m->RigMeshes[0].VertexNormals(), *m->heTopology, nullptr); // m->threadPool.get());
    State.m->ConstraintMeasurements = BodyMeasurement::GetBodyMeasurements(State.m->Constraints, State.m->RigMeshes[0].Vertices(), State.m->RawControls);
}

void BodyShapeEditor::UpdateGuiFromRawControls(State& state) const
{
    const Eigen::VectorXf prevRawControls = state.m->RawControls;

    state.m->GuiControls = Eigen::VectorXf::Zero(m->rigLogic->NumGUIControls());
    state.m->GuiControls(m->globalIndices) = m->rawToGlobalGuiControlsSolveMatrix * prevRawControls;
    Eigen::VectorXf newRawControls = m->rigLogic->EvaluateRawControls(state.m->GuiControls).Value();
    for (int vID = 0; vID < (int)m->rawToGuiControls.size(); ++vID)
    {
        const int guiID = m->rawToGuiControls[vID];
        if (guiID >= 0)
        {
            state.m->GuiControls[guiID] += prevRawControls[vID] - newRawControls[vID];
        }
    }
}

int BodyShapeEditor::NumLODs() const
{
    if (!m->combinedLodGenerationData)
    {
        return 1;
    }
    else
    {
        return static_cast<int>(m->combinedLodGenerationData->HigherLodMeshNames().size()) + 1;
    }
}

std::vector<int> FindMissing(int totalInputs, const std::vector<int>& selected)
{
    std::vector<bool> isSelected(totalInputs, false);

    for (int control : selected)
    {
        isSelected[control] = true;
    }

    std::vector<int> missing;
    for (int i = 0; i < totalInputs; ++i)
    {
        if (!isSelected[i])
        {
            missing.push_back(i);
        }
    }

    return missing;
}

std::vector<int> NonZeroMaskVerticesIntersection(const std::vector<int>& mapping, const std::vector<int>& mask)
{
    std::unordered_set<int> maskSet(mask.begin(), mask.end());
    std::vector<int> result;

    for (int idx : mapping)
    {
        if (maskSet.contains(idx))
        {
            result.push_back(idx);
        }
    }

    return result;
}

int ClosestIndex(int queryIndex, std::vector<int>& targetIndices, const Eigen::Matrix<float, 3, -1>& vertexPositions)
{
    float distance = 1e5f;
    int resultIndex = -1;

    const Eigen::Vector3f queryVertex = vertexPositions.col(queryIndex);
    for (int i = 0; i < (int)targetIndices.size(); ++i)
    {
        const Eigen::Vector3f targetVertex = vertexPositions.col(targetIndices[i]);
        float currentDistance = (targetVertex - queryVertex).norm();
        if (currentDistance < distance)
        {
            distance = currentDistance;
            resultIndex = targetIndices[i];
        }
    }

    return resultIndex;
}

const BodyJointEstimator& BodyShapeEditor::JointEstimator() { return m->jointEstimator; }

void BodyShapeEditor::UpdateSkinningAndRBF(const Eigen::VectorXf& rawControls, const Eigen::VectorXf& joints, std::shared_ptr<BodyGeometry<float>> poseGeometry, std::shared_ptr<BodyLogic<float>> poseLogic) const
{
    Eigen::Matrix3Xf restPoses = poseGeometry->GetJointRestPoses();
    // poseGeometry->GetVertexInfluenceWeights(0) = m->skinWeightsPCA.calculateResult(m->rawToGlobalGuiControlsSolveMatrix * rawControls);
    BodyGeometry<float>::State state;
    m->rigGeometry->EvaluateBodyGeometry(0, joints, rawControls, state);
    std::vector<Eigen::Affine3f> world = state.GetWorldMatrices();
    const SparseMatrix<float> jointVertexMatrix = m->jointEstimator.VertexJointMatrix().transpose();

    for (int jID : m->jointEstimator.SurfaceJoints())
    {
        SparseMatrix<float>::InnerIterator it(jointVertexMatrix, jID);
        const int vID = it.col();
        int parentID = m->rigGeometry->GetJointParentIndices()[jID];
        auto affine = world[jID]; 
        affine.translation() = state.Vertices().Matrix().col(vID);
        restPoses.col(jID) = (world[parentID].inverse() * affine).translation();
    }
    poseGeometry->GetJointRestPoses() = restPoses;
    poseGeometry->UpdateBindPoses();
    poseLogic->GetRbfJointMatrix(0) = m->rbfPCA.calculateResult(m->rawToGlobalGuiControlsSolveMatrix * rawControls);
}

Vector<float> BodyShapeEditor::SolveForTemplateMesh(State& State, const Eigen::Ref<const Eigen::Matrix<float, 3, -1>>& TargetMesh,  const FitToTargetOptions& options, ConstArrayView<float> inJointRotations, Eigen::VectorXf vertexWeightOverride, IterationFunc iterationFunc)
{
    std::vector<int> keyPoints = m->keypoints;
    auto poseGeometry = m->rigGeometry->Clone();
    auto poseLogic = m->poseLogic->Clone();
    int combinedVertexCount = m->rigGeometry->GetMesh(0).Vertices().cols();
    Eigen::Matrix<float, 3, -1> TargetMeshCombined(3, combinedVertexCount);
    State.m->JointDeltas.setZero(); 
    TargetMeshCombined.setZero();
    const auto& ArchBindMatrices = m->rigGeometry->GetBindMatrices();
    for (int i = 0; i < m->rigGeometry->NumJoints(); ++i)
    {
        State.m->JointBindMatrices[i].linear() = ArchBindMatrices[i].linear();
    }

    Vector<float> vertexWeights = Vector<float>::Zero(TargetMeshCombined.cols());

    if (TargetMesh.cols() != combinedVertexCount)
    {
        keyPoints.erase(std::remove_if(keyPoints.begin(), keyPoints.end(), [&](int vi)
                            { return m->combinedToBodyMapping[0].find(vi) == m->combinedToBodyMapping[0].end(); }),
            keyPoints.end());
        for (int i = 0; i < m->bodyToCombinedMapping[0].size(); ++i)
        {
            if (m->bodyToCombinedMapping[0][i] >= 0 && m->bodyToCombinedMapping[0][i] < combinedVertexCount)
            {
                TargetMeshCombined.col(m->bodyToCombinedMapping[0][i]) = TargetMesh.col(i);
            }
        }
        for (int idx : m->bodyToCombinedMapping[0])
        {
            vertexWeights[idx] = 1.0f;
        }
    }
    else
    {
        TargetMeshCombined = TargetMesh;
        vertexWeights.setConstant(1.0f);
    }
    for (int vID : keyPoints)
    {
        vertexWeights[vID] = 4.0f;
    }
    if(vertexWeightOverride.size() == vertexWeights.size() )
    {
        vertexWeights = vertexWeightOverride;
    }

    int iteration = 0;

    auto startsWith = [](const std::vector<std::string>& prefixes, const std::vector<std::string>& names)
    {
        std::vector<int> result;
        for (int i = 0; i < (int)names.size(); ++i)
        {
            const auto& name = names[i];
            for (const auto& prefix : prefixes)
            {
                if (name.starts_with(prefix))
                {
                    result.push_back(i);
                    break;
                }
            }
        }
        return result;
    };


    // std::function<void(Cost<float>&, std::shared_ptr<BodyGeometry<float>::State>)> addJointCost = [](Cost<float>&, std::shared_ptr<BodyGeometry<float>::State>) { };
    // if (TargetJoints.cols() > 0 && options.fitSkeleton)
    // {
    //     std::vector<int> jointIndices;
    //     for (int j : m->jointEstimator.CoreJoints())
    //     {
    //         // skipping root joint and invalid joints
    //         if (j != 0 && TargetJoints.col(j).squaredNorm() > 0.0f)
    //         {
    //             jointIndices.push_back(j);
    //         }
    //     }
    //     Vector<float> jointWeights = Vector<float>::Ones(jointIndices.size());
    //     std::vector<std::string> coreJointsNames;
    //     coreJointsNames.reserve(jointIndices.size());
    //     for (int ji : jointIndices)
    //     {
    //         coreJointsNames.push_back(poseGeometry->GetJointNames()[ji]);
    //     }

    //     const auto setJointWeight = [&](const std::vector<std::string>& joints, float value)
    //     {
    //         for (int i : startsWith(joints, coreJointsNames))
    //         {
    //             jointWeights(i) = value;
    //         }
    //     };
    //     setJointWeight({ "hand" }, 20.f);
    //     setJointWeight({ "pinky_01", "pinky_02", "ring_01", "ring_02", "middle_01", "middle_02", "index_01", "index_02", "thumb_01", "thumb_02" }, 5.0f);
    //     setJointWeight({ "pinky_03", "ring_03", "middle_03", "index_03", "thumb_03" }, 10.0f);
    //     setJointWeight({ "upperarm" }, 20.f);
    //     setJointWeight({ "thigh", "calf", "lowerarm" }, 20.0f);

    //     Eigen::Matrix<float, 3, -1> target(3, jointIndices.size());
    //     for (int i = 0; i < jointIndices.size(); ++i)
    //     {
    //         int idx = jointIndices[i];
    //         target.col(i) = TargetJoints.col(idx);
    //     }

    //     addJointCost = [jointIndices, target, jointWeights](Cost<float>& cost, std::shared_ptr<BodyGeometry<float>::State> state)
    //     {
    //         const auto& worldMatrices = state->GetWorldMatrices();
    //         const auto& worldMatricesJacobian = state->GetWorldMatricesJacobian();

    //         Eigen::Matrix<float, 3, -1> jointTranslations(3, jointIndices.size());
    //         auto jointTranslationJacobian = std::make_shared<Eigen::Matrix<float, -1, -1, Eigen::RowMajor>>(jointIndices.size() * 3, worldMatricesJacobian.cols());
    //         for (int i = 0; i < jointIndices.size(); i++)
    //         {
    //             int ji = jointIndices[i];
    //             int rowStart = ji * 12;
    //             jointTranslations.col(i) = worldMatrices[ji].translation();
    //             jointTranslationJacobian->middleRows(i * 3, 3) = worldMatricesJacobian.block(rowStart + 9, 0, 3, worldMatricesJacobian.cols());
    //         }
    //         auto jointWorldTranslations = DiffDataMatrix<float, 3, -1>(std::move(jointTranslations),
    //             std::make_shared<DenseJacobian<float>>(std::move(jointTranslationJacobian), 0));

    //         cost.Add(PointPointConstraintFunction<float, 3>::Evaluate(jointWorldTranslations,
    //                      target,
    //                      jointWeights,
    //                      1e-4f),
    //             1.0f, "Joints");
    //     };
    // }
    BoundedVectorVariable<float> pose { poseLogic->NumGUIControls() };
    Context<float> context {};
    SimpleGaussNewtonSolver<float> solver;
    std::vector<int> constantIndices {};
    bool solveForPose = options.solveForPose;

    if (solveForPose && options.enforceAnatomicalPose)
    {
        Eigen::Matrix<float, 2, -1> ranges = poseLogic->GuiControlRanges();
        for (int i : startsWith({ "local_" }, poseLogic->GuiControlNames()))
        {
            ranges.coeffRef(0,i) = -100.0f;
            ranges.coeffRef(1,i) = 100.0f;
        }
        pose.SetBounds(ranges);
        const auto directJointIndices = startsWith({ "pose_driver" }, poseLogic->GuiControlNames());
        constantIndices.insert(constantIndices.end(), directJointIndices.begin(), directJointIndices.end());
    }

    if (solveForPose && inJointRotations.size() == static_cast<std::size_t>(m->rigGeometry->NumJoints()) * 3u)
    {
        Vector<float> poseWithLockedValues = pose.Value();
        
        const auto& jointHierarchy = m->rigGeometry->GetJointParentIndices();

        const auto getJointParent = [&jointHierarchy](std::uint16_t jointIndex)
        {
            return jointHierarchy[jointIndex];
        };

        const std::vector<Eigen::Transform<float, 3, Eigen::Affine>>& jointMatrices = State.GetJointBindMatrices();
        std::vector<float> jointRotations;
        jointRotations.resize(m->rigGeometry->NumJoints() * 3);

        for (std::uint16_t jointIndex = 0; jointIndex < m->rigGeometry->NumJoints(); jointIndex++)
        {
            Eigen::Transform<float, 3, Eigen::Affine> localTransform;
            const int parentJointIndex = getJointParent(jointIndex);
            if (parentJointIndex >= 0)
            {
                auto parentTransform = jointMatrices[parentJointIndex];
                localTransform = parentTransform.inverse() * jointMatrices[jointIndex];
            }
            else
            {
                localTransform = jointMatrices[jointIndex];
            }

            Eigen::Matrix<float, 3, 3> InJointRotation = EulerXYZ(inJointRotations[jointIndex * 3 + 0], inJointRotations[jointIndex * 3 + 1], inJointRotations[jointIndex * 3 + 2]);
            Eigen::Vector3f euler = RotationMatrixToEulerXYZ<float>( localTransform.linear().inverse() * InJointRotation);
            jointRotations[jointIndex * 3 + 0] = euler[0]; 
            jointRotations[jointIndex * 3 + 1] = euler[1]; 
            jointRotations[jointIndex * 3 + 2] = euler[2]; 
        }

        for (auto ji : m->jointEstimator.CoreJoints()) 
        {
            if (ji < 2)
            {
                // we want to skip pelvis and root
                continue;
            }
            std::string jointControlNamePrefix = "pose_driver_" + m->rigGeometry->GetJointNames()[ji] + ".";
            auto jointControlIndices = startsWith({jointControlNamePrefix}, poseLogic->GuiControlNames());
            if (jointControlIndices.size() == 3)
            {
                poseWithLockedValues(jointControlIndices[0]) = jointRotations[ji * 3 + 0];
                poseWithLockedValues(jointControlIndices[1]) = jointRotations[ji * 3 + 1];
                poseWithLockedValues(jointControlIndices[2]) = jointRotations[ji * 3 + 2];
            }
        }
        pose = poseWithLockedValues;
        solveForPose = false;
    }
    if(!solveForPose)
    {
        const auto directIndices = startsWith({ "pose_driver" }, poseLogic->GuiControlNames());
        constantIndices.insert(constantIndices.end(), directIndices.begin(), directIndices.end());
        const auto anatomicalIndices = startsWith({ "pose_ana" }, poseLogic->GuiControlNames());
        constantIndices.insert(constantIndices.end(), anatomicalIndices.begin(), anatomicalIndices.end());
        const auto pelvisRotation = startsWith({ "pose_rigid_pelvis.r" }, poseLogic->GuiControlNames());
        const auto rootRotation = startsWith({ "pose_rigid_root.r" }, poseLogic->GuiControlNames());
        constantIndices.insert(constantIndices.begin(), pelvisRotation.begin(), pelvisRotation.end());
        constantIndices.insert(constantIndices.begin(), rootRotation.begin(), rootRotation.end());
    }
    State.SetApplyFloorOffset(false);

    pose.MakeIndividualIndicesConstant(constantIndices);
    auto state = m->StatePoolJacobian.Aquire();
    std::function<Cost<float>(Context<float>*)> costFunctionAll = [&](Context<float>* context)
    {
        Cost<float> cost;
        DiffData<float> guiControls = pose.Evaluate(context);
        const DiffData<float> rawControls = poseLogic->EvaluateRawControls(guiControls);
        const DiffData<float> joints = poseLogic->EvaluateJoints(0, rawControls);
        const DiffData<float> rbfPsd = m->rbfLogic->EvaluatePoseControlsFromJoints(joints, true);
        //UpdateSkinningAndRBF(rawControls.Value().head(m->rigLogic->NumRawControls()), joints.Value(), poseGeometry, poseLogic);
        const DiffData<float> rbfJoints = poseLogic->EvaluateRbfJoints(0, rbfPsd);
        const DiffData<float> twistedJoints = m->twistSwingLogic->EvaluateJointsFromJoints(joints + rbfJoints);
        poseGeometry->EvaluateBodyGeometry(0, twistedJoints, rawControls, *state);
        cost.Add(PointPointConstraintFunction<float, 3>::Evaluate(state->Vertices(), TargetMeshCombined,
                     vertexWeights,
                     1e-4f),
            1.0f, "Mesh");

        cost.Add(LimitConstraintFunction<float>::Evaluate(
                         guiControls, *poseLogic, 0.01f), 1.0f, "Limit");
        // addJointCost(cost, state);
        iterationFunc(state->Vertices().Matrix(), iteration, cost.Value().squaredNorm(), state->GetWorldMatrices());
        iteration++;

        return cost;
    };

    solver.Solve(costFunctionAll, context, options.iterations, 0.0f, options.epsilon1, options.epsilon2, m->threadPool.get());
    State.m->JointDeltas.setZero();
    State.m->RawControls.conservativeResize(m->rawLocalIndices.size());
    const DiffData<float> rawControls = poseLogic->EvaluateRawControls(pose.Value());
    {
        const DiffData<float> joints = poseLogic->EvaluateJoints(0, rawControls);
        const DiffData<float> rbfPsd = m->rbfLogic->EvaluatePoseControlsFromJoints(joints, true);
        const DiffData<float> rbfJoints = poseLogic->EvaluateRbfJoints(0, rbfPsd);
        const DiffData<float> twistedJoints = m->twistSwingLogic->EvaluateJointsFromJoints(joints + rbfJoints);
        poseGeometry->EvaluateBodyGeometry(0, twistedJoints, rawControls, *state);
    }
    
    State.m->VertexDeltas = poseGeometry->EvaluateInverseSkinning(0, *state, TargetMeshCombined) - poseGeometry->EvaluateInverseSkinning(0, *state, state->Vertices().Matrix());

    State.m->RawControls.head(m->rigLogic->NumRawControls()) = rawControls.Value().head(m->rigLogic->NumRawControls());
    State.m->CustomPose = poseLogic->EvaluateJoints(0, rawControls).Value();
    if (TargetMesh.cols() != TargetMeshCombined.cols())
    {
        Eigen::Matrix<float, 3, -1> tempTargetDeltas = Eigen::Matrix<float, 3, -1>::Zero(3, TargetMeshCombined.cols());
        for (auto [i, _] : m->combinedToBodyMapping[0])
        {
            tempTargetDeltas.col(i) = State.m->VertexDeltas.col(i);
        }
        State.m->VertexDeltas = tempTargetDeltas;
        Eigen::Matrix<float, 3 , -1> vertices = (m->identityVertexEvaluationMatrix * State.m->RawControls + m->rigGeometry->GetMesh(0).Vertices().reshaped()).reshaped(3, TargetMeshCombined.cols());

        DeformationModelVertex<float> defModelVertex;

        auto config = defModelVertex.GetConfiguration();
        config["vertexOffsetRegularization"] = 0.0f;
        config["vertexLaplacian"] = 5.0f;
        defModelVertex.SetConfiguration(config);
        defModelVertex.SetMeshTopology(m->rigGeometry->GetMesh(0));
        defModelVertex.SetRestVertices(vertices);
        defModelVertex.MakeVerticesConstant(m->bodyToCombinedMapping[0]);
        defModelVertex.SetVertexOffsets(State.m->VertexDeltas);

        std::function<DiffData<float>(Context<float>*)> evaluationFunction = [&](Context<float>* context)
        {
            Cost<float> cost;
            cost.Add(defModelVertex.EvaluateModelConstraints(context));
            return cost.CostToDiffData();
        };

        const float startEnergy = evaluationFunction(nullptr).Value().squaredNorm();
        if (GaussNewtonSolver<float>().Solve(evaluationFunction, 3))
        {
            const float finalEnergy = evaluationFunction(nullptr).Value().squaredNorm();
            LOG_INFO("energy changed from {} to {}", startEnergy, finalEnergy);
        }
        else
        {
            LOG_ERROR("could not solve optimization problem");
        }
        iterationFunc(defModelVertex.DeformedVertices(), 0, 0, std::vector<Eigen::Affine3f>{});
        State.m->VertexDeltas = defModelVertex.DeformedVertices() - vertices;
    }
    
    // if (TargetJoints.cols() > 0 && options.fitSkeleton)
    // {
    //     Maybe we can use this part for some joint assisted solving, but for now its not needed 
    //     DiffData<float> guiControls = pose.Value();
    //     const DiffData<float> rawControls = poseLogic->EvaluateRawControls(guiControls);
    //     const DiffData<float> joints = poseLogic->EvaluateJoints(0, rawControls);

    //     BodyGeometry<float>::State restState;
    //     poseGeometry->EvaluateBodyGeometry(0, Eigen::VectorXf::Zero(joints.Value().rows()).eval(), Eigen::VectorXf::Zero(rawControls.Value().rows()).eval(), restState);

    //     std::vector<Eigen::Affine3f> estimatedWorldPose(state->GetWorldMatrices());
    //     std::vector<Eigen::Affine3f> estimatedLocalPose(estimatedWorldPose.size());
    //     auto estimatedJointDiff = Eigen::VectorXf::Zero( estimatedWorldPose.size()*9).eval();

    //     for(int j : m->jointEstimator.CoreJoints())
    //     {
    //         if( j != 0 && TargetJoints.col(j).squaredNorm() > 0.0f)
    //         {
    //             estimatedWorldPose[j].translation() = TargetJoints.col(j);
    //         }
    //     }
    //     for (int j : m->jointEstimator.CoreJoints()) {
    //         int parent = m->rigGeometry->GetParentIndex(j);
    //         if (parent < 0) {
    //             estimatedLocalPose[j] = estimatedWorldPose[j];
    //         } else {
    //             estimatedLocalPose[j] = estimatedWorldPose[parent].inverse() * estimatedWorldPose[j];
    //         }
    //         const Eigen::Matrix4f local = state->GetLocalMatrices()[j].matrix();
    //         const Eigen::Matrix3f rs = restState.GetLocalMatrices()[j].matrix().topLeftCorner<3, 3>().inverse() * local.topLeftCorner<3, 3>();
    //         const Eigen::Vector3f r = RotationMatrixToEulerXYZ(rs);
    //         const Eigen::Vector3f t = estimatedLocalPose[j].translation() - restState.GetLocalMatrices()[j].translation();
    //         estimatedJointDiff(j * 9 + 0) = t.x();
    //         estimatedJointDiff(j * 9 + 1) = t.y();
    //         estimatedJointDiff(j * 9 + 2) = t.z();
    //         estimatedJointDiff(j * 9 + 3) = r.x();
    //         estimatedJointDiff(j * 9 + 4) = r.y();
    //         estimatedJointDiff(j * 9 + 5) = r.z();
    //     }
    //     const auto correctedWorldMatrices = poseGeometry->EvaluateBodyGeometry(0, estimatedJointDiff, Eigen::VectorXf::Zero(rawControls.Value().rows()).eval()).GetWorldMatrices();
    //     State.m->JointDeltas.conservativeResize(3, correctedWorldMatrices.size());
    //     for( int i =0; i < correctedWorldMatrices.size(); ++i)
    //     {
    //         State.m->JointDeltas.col(i) =  correctedWorldMatrices[i].translation() - state->GetWorldMatrices()[i].translation();
    //     }
    // }
    State.SetApplyFloorOffset(false);
    UpdateGuiFromRawControls(State);
    State.m->GuiControlsPrior = State.m->GuiControls;
    EvaluateState(State);
    return pose.Value();
}

void BodyShapeEditor::SetNeutralJointsTranslations(State& state, const Eigen::Ref<const Eigen::Matrix<float, 3, -1>>& InJoints)
{
    Eigen::Matrix<float, 3, -1> jointPositions(3, (int)state.GetJointBindMatrices().size());
    for (size_t i = 0; i < m->rigGeometry->GetBindMatrices().size(); ++i)
    {
        jointPositions.col(i) = state.GetJointBindMatrices()[i].translation();
    }
    state.m->JointDeltas = InJoints - (jointPositions - state.m->JointDeltas);
    EvaluateState(state);
}

void BodyShapeEditor::SetNeutralJointRotations(State& state, av::ConstArrayView<float> inJointRotations)
{
    std::vector<Eigen::Transform<float, 3, Eigen::Affine>>& jointMatrices = state.m->JointBindMatrices;
    std::vector<float> jointRotations;
    jointRotations.resize(m->rigGeometry->NumJoints() * 3);
    const auto getJointParent = [this](std::uint16_t jointIndex)
    {
        return m->rigGeometry->GetJointParentIndices()[jointIndex];
    };
    for (std::uint16_t jointIndex = 0; jointIndex < m->rigGeometry->NumJoints(); jointIndex++)
    {
        Eigen::Transform<float, 3, Eigen::Affine>& localTransform = jointMatrices[jointIndex];
        const int parentJointIndex = getJointParent(jointIndex);
        if (parentJointIndex > -1)
        {
            auto parentTransform = jointMatrices[parentJointIndex];
            localTransform = parentTransform.inverse() * jointMatrices[jointIndex];
        }
        else
        {
            localTransform = jointMatrices[jointIndex];
        }
        Eigen::Matrix<float, 3, 3> InJointRotation = EulerXYZ(inJointRotations[jointIndex * 3 + 0], inJointRotations[jointIndex * 3 + 1], inJointRotations[jointIndex * 3 + 2]);
        localTransform.linear() = InJointRotation;
        if(parentJointIndex > -1)
        {
            localTransform = jointMatrices[parentJointIndex] * localTransform;
        }
    } 
}

void BodyShapeEditor::VolumetricallyFitHandAndFeetJoints(State& State)
{
    auto translationEstimates = m->jointEstimator.EstimateJointWorldTranslations(State.m->RigMeshes[0].Vertices());
    const std::vector<int>& jointHierarchy = m->rigGeometry->GetJointParentIndices();
    const std::vector<std::string>& jointNames = m->rigGeometry->GetJointNames();
    const int n = static_cast<int>(jointHierarchy.size());
    
    std::unordered_map<std::string, int> nameToIdx;
    nameToIdx.reserve(n);
    for (int i = 0; i < n; ++i) 
    {
        nameToIdx.emplace(jointNames[i], i);
    }
    std::vector<int> jointIndicesToUpdate; 
    std::vector<char> mark(n, 0);
    auto markIfFound = [&](const char* nm){
        auto it = nameToIdx.find(nm);
        if (it != nameToIdx.end())
        {
            mark[it->second] = 1;
            jointIndicesToUpdate.push_back(it->second);
        }
    };
    markIfFound("hand_l");
    markIfFound("hand_r");
    markIfFound("foot_l");
    markIfFound("foot_r");
    
    for (int i = 0; i < n; ++i) {
        int p = jointHierarchy[i];
        if (mark[i] != 1 && p > 0 && mark[p] == 1) 
        {
            mark[i] = 1;
            jointIndicesToUpdate.push_back(i);
        }
    }

    std::vector<Eigen::Transform<float, 3, Eigen::Affine>> jointMatrices = State.GetJointBindMatrices();
    Eigen::Matrix<float, 3, -1> jointPositions(3, (int)jointMatrices.size());
    for (size_t i = 0; i < m->rigGeometry->GetBindMatrices().size(); ++i)
    {
        jointPositions.col(i) = jointMatrices[i].translation();
    }
    for (int  i : jointIndicesToUpdate) {
        jointPositions.col(i) = translationEstimates.col(i);
        jointMatrices[i].translation() = translationEstimates.col(i);
    }
    SetNeutralJointsTranslations(State, jointPositions);
    
    jointIndicesToUpdate.push_back(jointHierarchy[nameToIdx["hand_l"]]);
    jointIndicesToUpdate.push_back(jointHierarchy[nameToIdx["hand_r"]]);
    jointIndicesToUpdate.push_back(jointHierarchy[nameToIdx["foot_l"]]);
    jointIndicesToUpdate.push_back(jointHierarchy[nameToIdx["foot_r"]]);
    m->jointEstimator.FixJointOrients(*m->rigGeometry, State.m->JointBindMatrices, State.GetMesh(0).Vertices());
    
}

void BodyShapeEditor::SetNeutralMesh(State& state, const Eigen::Ref<const Eigen::Matrix<float, 3, -1>>& inMesh)
{
    const auto baseMesh = state.GetMesh(0).Vertices() - state.m->VertexDeltas;
    const auto& mapping = m->bodyToCombinedMapping[0]; 
    if(inMesh.cols() == static_cast<int>(mapping.size()))
    {
        for (size_t i = 0; i < mapping.size(); ++i)
        {
            int targetCol = mapping[i];
            state.m->VertexDeltas.col(targetCol) = inMesh.col(i) - baseMesh.col(targetCol);
        }
    } else {
        state.m->VertexDeltas = inMesh - baseMesh; 
    }
    EvaluateState(state);
}

void BodyShapeEditor::Init(std::shared_ptr<BodyLogic<float>> bodyLogic,
    std::shared_ptr<BodyGeometry<float>> combinedBodyArchetypeGeometry,
    std::shared_ptr<RigLogic<float>> CombinedBodyRigLogic,
    std::shared_ptr<BodyGeometry<float>> bodyGeometry,
    av::ConstArrayView<BodyMeasurement> contours,
    const std::vector<std::map<std::string, std::map<std::string, float>>>& jointSkinningWeightLodPropagationMap,
    const std::vector<int>& maxSkinWeightsPerVertexForEachLod,
    std::shared_ptr<const LodGeneration<float>> combinedLodGenerationData,
    const std::map<std::string, VertexWeights<float>>& partWeights)
{
    m->minMeasurementInput.clear();
    m->maxMeasurementInput.clear();
	m->variableMinMeasurementInput.clear();
	m->variableMaxMeasurementInput.clear();
	m->heightConstraintIndex = -1;
    m->rigLogic = bodyLogic;
    m->rigGeometry = bodyGeometry;
    m->combinedBodyArchetypeRigGeometry = combinedBodyArchetypeGeometry;
    m->combinedLodGenerationData = combinedLodGenerationData;
    m->partWeights = partWeights;
    m->rbfControlOffset = CombinedBodyRigLogic->NumRawControls() + CombinedBodyRigLogic->NumMLControls() + CombinedBodyRigLogic->NumPsdControls();
    if (!m->threadPool)
    {
        m->threadPool = TaskThreadPool::GlobalInstance(true);
    }

    m->rigGeometry->SetThreadPool(m->threadPool);

    m->Constraints.assign(contours.begin(), contours.end());

    const std::vector<std::string>& guiControlNames = m->rigLogic->GuiControlNames();
    const std::vector<std::string>& rawControlNames = m->rigLogic->RawControlNames();
    if (CombinedBodyRigLogic)
    {
        m->jointGroupInputIndices = CombinedBodyRigLogic->GetJointGroupInputIndices();
        m->jointGroupOutputIndices = CombinedBodyRigLogic->GetJointGroupOutputIndices();
    }

    m->localIndices.clear();
    m->globalIndices.clear();
    m->poseIndices.clear();
    {
        for (int i = 0; i < int(guiControlNames.size()); ++i)
        {
            const std::string& name = guiControlNames[i];
            if (name.find("global_") != name.npos)
            {
                m->globalIndices.emplace_back(i);
            }
            else if (name.find("local_") != name.npos)
            {
                m->localIndices.emplace_back(i);
            }
            else if (name.find("pose_") != name.npos)
            {
                m->poseIndices.emplace_back(i);
            }
            else
            {
                CARBON_CRITICAL("unknown control \"{}\"", name);
            }
        }
    }

    m->rawLocalIndices.clear();
    m->rawPoseIndices.clear();
    {
        for (int i = 0; i < int(rawControlNames.size()); ++i)
        {
            const std::string& name = rawControlNames[i];
            if (name.find("local_") != name.npos)
            {
                m->rawLocalIndices.emplace_back(i);
            }
            else if (name.find("pose_") != name.npos)
            {
                m->rawPoseIndices.emplace_back(i);
            }
            else
            {
                CARBON_CRITICAL("unknown raw control \"{}\"", name);
            }
        }
    }


    Eigen::SparseMatrix<float, Eigen::ColMajor> invertedJointMatrix = m->rigLogic->GetJointMatrix(0);

    std::map<std::string, std::vector<int>> skeletonPcaControls;
    std::map<std::string, std::vector<int>> shapePcaControls;
    std::map<std::string, std::string> symmetricPartMapping;
    std::set<std::string> regionNamesSet;
    std::vector<int> jointControls;
    std::vector<int> shapeControls;
    for (int i = 0; i < int(rawControlNames.size()); ++i)
    {
        std::string name = rawControlNames[i];
        size_t suffixPos = name.find_last_of('_');
        if (suffixPos != std::string::npos)
        {
            name = name.substr(0, suffixPos);
        }
        const bool isLeft = StringEndsWith(name, "_l");
        const bool isRight = StringEndsWith(name, "_r");
        // const bool isCenter = !isLeft && !isRight;
        const bool isPose = StringStartsWith(name, "pose");
        std::string partname = name;
        if (StringStartsWith(name, "local_joint_"))
        {
            // skeleton pca
            partname = ReplaceSubstring(partname, "local_", "");
            skeletonPcaControls[partname].push_back(i);
            jointControls.push_back(i);

            auto regionName = ReplaceSubstring(partname, "joint_", "");
            for (Eigen::SparseMatrix<float, Eigen::ColMajor>::InnerIterator it(invertedJointMatrix, i); it; ++it)
            {
                m->regionToJoints[regionName].emplace(static_cast<int>(it.row() / 9));
            }
        }
        else if (StringStartsWith(name, "local_"))
        {
            // shape pca
            partname = ReplaceSubstring(partname, "local_", "");
            shapePcaControls[partname].push_back(i);
            shapeControls.push_back(i);
        }
        else if (!isPose)
        {
            LOG_ERROR("unknown control {}", rawControlNames[i]);
        }

        if (!isPose)
        {
            if (isLeft)
            {
                symmetricPartMapping[partname] = partname.substr(0, partname.size() - 2) + "_r";
            }
            else if (isRight)
            {
                symmetricPartMapping[partname] = partname.substr(0, partname.size() - 2) + "_l";
            }
            else
            {
                symmetricPartMapping[partname] = partname;
            }
            regionNamesSet.insert(partname);
        }
    }
    m->skeletonPcaControls = skeletonPcaControls;
    m->shapePcaControls = shapePcaControls;
    m->symmetricPartMapping = symmetricPartMapping;
    m->regionNames = std::vector<std::string>(regionNamesSet.begin(), regionNamesSet.end());

    m->rawToGuiControls = std::vector<int>(rawControlNames.size(), -1);
    m->guiToRawControls = std::vector<int>(guiControlNames.size(), -1);
    for (int i = 0; i < (int)rawControlNames.size(); ++i)
    {
        m->rawToGuiControls[i] = GetItemIndex<std::string>(m->rigLogic->GuiControlNames(), rawControlNames[i]);
    }
    for (int i = 0; i < (int)guiControlNames.size(); ++i)
    {
        m->guiToRawControls[i] = GetItemIndex<std::string>(m->rigLogic->RawControlNames(), guiControlNames[i]);
    }

    // get mapping matrix from gui to raw controls
    m->guiToRawMappingMatrix = Eigen::SparseMatrix<float, Eigen::RowMajor>(m->rigLogic->NumRawControls(), m->rigLogic->NumGUIControls());
    for (const auto& mapping : m->rigLogic->GuiToRawMapping())
    {
        m->guiToRawMappingMatrix.coeffRef(mapping.outputIndex, mapping.inputIndex) += mapping.slope;
        if (mapping.cut != 0)
        {
            CARBON_CRITICAL("invalid cut value {}", mapping.cut);
        }
    }
    m->guiToRawMappingMatrix.makeCompressed();

    {
        Eigen::MatrixXf A = Eigen::MatrixXf(m->guiToRawMappingMatrix)(Eigen::all, m->globalIndices);
        m->rawToGlobalGuiControlsSolveMatrix = (A.transpose() * A).inverse() * A.transpose();
    }

    m->meshTriangles.resize(NumLODs());
    // get lod 0 from the pca model rig geometry, as we will always have this (combinedBodyArchetypeRigGeometry is optional)
    Mesh<float> triMesh = m->rigGeometry->GetMesh(0);
    triMesh.Triangulate();
    m->meshTriangles[0] = triMesh.Triangles();
    m->triTopology = std::make_shared<Mesh<float>>(triMesh);
    m->heTopology = std::make_shared<HalfEdgeMesh<float>>(triMesh);

    if (m->combinedBodyArchetypeRigGeometry)
    {
        for (size_t i = 1; i < static_cast<size_t>(NumLODs()); ++i)
        {
            triMesh = m->combinedBodyArchetypeRigGeometry->GetMesh(static_cast<int>(i));
            triMesh.Triangulate();
            m->meshTriangles[i] = triMesh.Triangles();
        }
    }

    m->symControls = std::make_unique<SymmetricControls<float>>(*m->rigLogic);
    BoundedVectorVariable<float> pose { m->rigLogic->NumGUIControls() };
    Eigen::VectorXf guiWeight = Eigen::VectorXf::Zero(m->rigLogic->NumGUIControls());
    for (const auto& index : m->localIndices)
    {
        guiWeight[index] = 1.0f;
    }
    for (const auto& index : m->globalIndices)
    {
        guiWeight[index] = 0.33f;
    }
    m->gwm = SparseMatrix<float>(guiWeight.size(), guiWeight.size());
    m->gwm.setIdentity();
    m->gwm.diagonal() = guiWeight;

    m->maxSkinWeights = maxSkinWeightsPerVertexForEachLod;

    m->jointSkinningWeightLodPropagationMap = jointSkinningWeightLodPropagationMap;

    {
        // create a linear evaluation matrix
        Eigen::MatrixXf identityEvaluationMatrix = Eigen::MatrixXf::Zero(m->rigGeometry->GetMesh(0).NumVertices() * 3, (int)m->rawLocalIndices.size());
        Eigen::MatrixXf jointEvaluationMatrix = Eigen::MatrixXf::Zero(m->rigGeometry->GetBindMatrices().size() * 3, (int)m->rawLocalIndices.size());
        Eigen::VectorXf zeroRawControls = Eigen::VectorXf::Zero(m->rigLogic->NumRawControls());
        DiffData<float> zeroJoints = m->rigLogic->EvaluateJoints(0, zeroRawControls);
        BodyGeometry<float>::State zeroState;
        m->rigGeometry->EvaluateBodyGeometry(0, zeroJoints, zeroRawControls, zeroState);
        Eigen::Matrix3Xf zeroVertices = zeroState.Vertices().Matrix();

        auto calcVertexEvalMatrices = [&](int start, int end)
        {
            const auto& blendShapeMap = m->rigGeometry->GetBlendshapeMap(0);
            const auto& blendShapes = m->rigGeometry->GetBlendshapeMatrix(0);
            for (int i = start; i < end; ++i)
            {
                const int rawControlIndex = shapeControls[i];
                identityEvaluationMatrix.col(rawControlIndex) = blendShapes.col(blendShapeMap[rawControlIndex]);
            }
        };

        auto calcJointMatrices = [&](int start, int end)
        {
            BodyGeometry<float>::State geometryState;
            for (int i = start; i < end; ++i)
            {
                const int rawControlIndex = jointControls[i];
                Eigen::VectorXf rawControls = Eigen::VectorXf::Zero(m->rigLogic->NumRawControls());
                rawControls[rawControlIndex] = 1.0f;
                DiffData<float> joints = m->rigLogic->EvaluateJoints(0, rawControls);
                m->rigGeometry->EvaluateBodyGeometry(0, joints, rawControls, geometryState);
                identityEvaluationMatrix.col(rawControlIndex) = (geometryState.Vertices().Matrix() - zeroVertices).reshaped();
                for (int ji = 0; ji < (int)geometryState.GetWorldMatrices().size(); ++ji)
                {
                    jointEvaluationMatrix.col(rawControlIndex).segment(3 * ji, 3) = geometryState.GetWorldMatrices()[ji].translation() - m->rigGeometry->GetBindMatrices()[ji].translation();
                }
            }
        };

        m->threadPool->AddTaskRangeAndWait((int)jointControls.size(), calcJointMatrices);
        m->threadPool->AddTaskRangeAndWait((int)shapeControls.size(), calcVertexEvalMatrices);
        m->identityVertexEvaluationMatrix = identityEvaluationMatrix.sparseView(0, 0);
        m->identityVertexEvaluationMatrix.makeCompressed();

        m->identityJointEvaluationMatrix = jointEvaluationMatrix.sparseView(0, 0);
        m->identityJointEvaluationMatrix.makeCompressed();

        // create the identity evaluation matrix based on the symmetric constrols
        const auto& symToGuiMat = m->symControls->SymmetricToGuiControlsMatrix();
        const auto& guiToRawMat = m->guiToRawMappingMatrix;
        Eigen::SparseMatrix<float, Eigen::RowMajor> rawLocalIndicesMat(m->rawLocalIndices.size(), m->rawLocalIndices.size());
        for (int i = 0; i < m->rawLocalIndices.size(); ++i)
        {
            rawLocalIndicesMat.coeffRef(i, m->rawLocalIndices[i]) = 1.0f;
        }
        rawLocalIndicesMat.makeCompressed();
        m->symmetricIdentityVertexEvaluationMatrix = m->identityVertexEvaluationMatrix * (rawLocalIndicesMat * guiToRawMat * symToGuiMat);
    }

    m->Constraints = CreateState()->m->Constraints;

    // retrieve floor index
    for (size_t i = 0; i < m->Constraints.size(); ++i)
    {
        if (m->Constraints[i].GetName() == "Height")
        {
            m->floorIndex = m->Constraints[i].GetVertexIDs().front();
            for (int vID : m->Constraints[i].GetVertexIDs())
            {
                if (m->rigGeometry->GetMesh(0).Vertices().col(vID)[1] < m->rigGeometry->GetMesh(0).Vertices().col(m->floorIndex)[1])
                {
                    m->floorIndex = vID;
                }
            }
        }
    }
}

std::shared_ptr<BodyLogic<float>> BodyShapeEditor::GetBodyLogic() const { return m->poseLogic; }

std::vector<std::vector<int>> ReadBodyToCombinedMapping(const JsonElement& json)
{
    std::vector<std::vector<int>> mappings {};

    if (!json.Contains("body_to_combined"))
    {
        CARBON_CRITICAL("Invalid json file. Missing \"body_to_combined\" mapping.");
    }

    const std::uint16_t LODCount = 4u;

    if (json["body_to_combined"].Size() == LODCount)
    {
        mappings = json["body_to_combined"].Get<std::vector<std::vector<int>>>();
    }
    else
    {
        mappings.push_back(json["body_to_combined"].Get<std::vector<int>>());
        mappings.resize(LODCount);
    }
    return mappings;
}

std::vector<int> ReadKeypoints(const JsonElement& json)
{
    std::vector<int> keypoints {};

    if (!json.Contains("keypoints"))
    {
        CARBON_CRITICAL("Invalid json file. Missing \"keypoints\" mapping.");
    }

    const auto& keyPointsJson = json["keypoints"].Array();
    for (const auto& keyPoint : keyPointsJson)
    {
        keypoints.push_back(keyPoint["index"].Get<int>());
    }
    return keypoints;
}

std::vector<std::vector<int>> ReadBodyToCombinedMapping(const std::string& jsonString)
{
    const JsonElement json = ReadJson(jsonString);
    return ReadBodyToCombinedMapping(json);
}

void BodyShapeEditor::Init(const dna::Reader* reader,
    trio::BoundedIOStream* rbfModelStream,
    trio::BoundedIOStream* skinModelStream,
    dna::Reader* InCombinedArchetypeBodyDnaReader,
    const std::vector<std::map<std::string,
        std::map<std::string,
            float>>>& JointSkinningWeightLodPropagationMap,
    const std::vector<int>& maxSkinWeightsPerVertexForEachLod,
    std::shared_ptr<const LodGeneration<float>> combinedLodGenerationData)
{
    if (!m->threadPool)
    {
        m->threadPool = TaskThreadPool::GlobalInstance(true);
    }
    auto rigLogic = std::make_shared<BodyLogic<float>>();
    auto rigGeometry = std::make_shared<BodyGeometry<float>>(m->threadPool);
    std::shared_ptr<BodyGeometry<float>> combinedArchetypeRigGeometry = nullptr;
    std::shared_ptr<RigLogic<float>> combinedArchetypeRigLogic;
    if (!rigLogic->Init(reader))
    {
        CARBON_CRITICAL("failed to decode rig");
    }
    if (!rigGeometry->Init(reader))
    {
        CARBON_CRITICAL("failed to decode rig");
    }
    if (InCombinedArchetypeBodyDnaReader)
    {
        combinedArchetypeRigGeometry = std::make_shared<BodyGeometry<float>>(m->threadPool);
        if (!combinedArchetypeRigGeometry->Init(InCombinedArchetypeBodyDnaReader))
        {
            CARBON_CRITICAL("failed to decode body archetype");
        }
        combinedArchetypeRigLogic = std::make_shared<RigLogic<float>>();
        if (!combinedArchetypeRigLogic->Init(InCombinedArchetypeBodyDnaReader))
        {
            CARBON_CRITICAL("failed to decode body archetype");
        }
    }

    const auto pcaJsonStringView = reader->getMetaDataValue("pca_model");
    const JsonElement pcaModelJson = ReadJson(std::string { pcaJsonStringView.data(), pcaJsonStringView.size() });
    const std::vector<BodyMeasurement> contours = BodyMeasurement::FromJSON(pcaModelJson, rigGeometry->GetMesh(0).Vertices());

    if (pcaModelJson.Contains("joint_estimator"))
    {
        m->jointEstimator.Init(pcaModelJson["joint_estimator"]);
    }

    if (pcaModelJson.Contains("pose_logic"))
    {
        std::vector<std::string> poseLogicLines;
        for (const auto& line : pcaModelJson["pose_logic"].Array())
        {
            poseLogicLines.push_back(line.String());
        }
        m->poseLogic = PoseLogicFromString(poseLogicLines, rigLogic, rigGeometry, m->jointEstimator.CoreJoints());
        m->poseLogic->InitRBFJointMatrix(InCombinedArchetypeBodyDnaReader);
        m->rbfLogic = std::make_shared<RBFLogic<float>>();
        m->rbfLogic->Init(InCombinedArchetypeBodyDnaReader);
        m->twistSwingLogic = std::make_shared<TwistSwingLogic<float>>();
        m->twistSwingLogic->Init(InCombinedArchetypeBodyDnaReader, true);
    }

    if (pcaModelJson.Contains("solve_hierarchy"))
    {
        m->solveSteps = pcaModelJson["solve_hierarchy"].Get<std::vector<std::pair<std::vector<std::string>, std::vector<std::string>>>>();
        for (auto& pair : m->solveSteps)
        {
            for (auto& name : pair.first)
            {
                name = BodyMeasurement::GetAlias(name);
            }
        }
    }
    if (pcaModelJson.Contains("model_version"))
    {
        m->ModelVersion = pcaModelJson["model_version"].Get<std::string>();
    }
    else
    {
        m->ModelVersion = "0.4.4";
    }

    std::map<std::string, VertexWeights<float>> partWeights;
    if (pcaModelJson.Contains("part_weights"))
    {
        partWeights = VertexWeights<float>::LoadAllVertexWeights(pcaModelJson["part_weights"], rigGeometry->GetMesh(0).NumVertices());
    }
    if (skinModelStream == nullptr)
    {
        m->skinWeightsPCA.ReadFromDNA(reader, "skin_model");
    }
    else
    {
        m->skinWeightsPCA.ReadFromStream(skinModelStream);
    }

    for (std::uint32_t i = 0; i < reader->getMetaDataCount(); ++i)
    {
        auto keyView = reader->getMetaDataKey(i); 
        const char* prefix = "skin_model-";
        size_t prefixLen = std::strlen(prefix);
        if (keyView.size() >= prefixLen && std::strncmp(keyView.data(), prefix, prefixLen) == 0)
        {
            auto skinModelVersionView = keyView.subview(prefixLen, keyView.size() - prefixLen);
            std::string skinModelVersion {skinModelVersionView.begin(), skinModelVersionView.end()};
            SparseMatrixPCA smPCA;
            smPCA.ReadFromDNA(reader, {keyView.begin(), keyView.end()}); 
            m->skinningModels.try_emplace(skinModelVersion, std::move(smPCA)); 
        }
    }

    if (rbfModelStream == nullptr)
    {
        m->rbfPCA.ReadFromDNA(reader, "rbf_model");
    }
    else
    {
        m->rbfPCA.ReadFromStream(rbfModelStream);
    }
    Init(rigLogic,
        combinedArchetypeRigGeometry,
        combinedArchetypeRigLogic,
        rigGeometry,
        contours,
        JointSkinningWeightLodPropagationMap,
        maxSkinWeightsPerVertexForEachLod,
        combinedLodGenerationData,
        partWeights);

    m->bodyToCombinedMapping = ReadBodyToCombinedMapping(pcaModelJson);
    m->keypoints = ReadKeypoints(pcaModelJson);

    // create the reverse mapping
    m->combinedToBodyMapping = std::vector<std::map<int, int>>(m->bodyToCombinedMapping.size());
    for (size_t lod = 0; lod < m->bodyToCombinedMapping.size(); ++lod)
    {
        for (size_t i = 0; i < m->bodyToCombinedMapping[lod].size(); ++i)
        {
            const int combinedIndex = m->bodyToCombinedMapping[lod][i];
            m->combinedToBodyMapping[lod][combinedIndex] = int(i);
        }
    }
}

void BodyShapeEditor::SetFittingVertexIDs(const std::vector<int>& vertexIds) { m->combinedFittingIndices = vertexIds; }

void BodyShapeEditor::SetNeckSeamVertexIDs(const std::vector<std::vector<int>>& vertexIds)
{
    m->neckSeamIndices = vertexIds;

    // set up skinning weight snap configs from the neck seam indices
    m->skinningWeightSnapConfigs.resize(m->combinedBodyArchetypeRigGeometry->GetNumLODs() - 1);
    for (int lod = 1; lod < m->combinedBodyArchetypeRigGeometry->GetNumLODs(); ++lod)
    {
        m->skinningWeightSnapConfigs[size_t(lod - 1)] = m->CalcNeckSeamSkinningWeightsSnapConfig(lod);
    }
}

void BodyShapeEditor::SetBodyToCombinedMapping(int lod, const std::vector<int>& bodyToCombinedMapping)
{
    if (lod >= m->bodyToCombinedMapping.size())
    {
        m->bodyToCombinedMapping.resize(lod + 1u);
    }
    m->bodyToCombinedMapping[lod].assign(bodyToCombinedMapping.begin(),
        bodyToCombinedMapping.end());
}

const std::vector<int>& BodyShapeEditor::GetBodyToCombinedMapping(int lod) const
{
    return m->bodyToCombinedMapping[lod];
}

void BodyShapeEditor::Private::SetMinMaxMeasurements(const State& State)
{
	minMeasurementInput.resize(Constraints.size());
	maxMeasurementInput.resize(Constraints.size());
	variableMinMeasurementInput.resize(Constraints.size());
	variableMaxMeasurementInput.resize(Constraints.size());

	std::vector<int> missingIndices {};
	for (int i = 0; i < Constraints.size(); i++)
	{
		minMeasurementInput[i] = Constraints[i].GetFixedMinInput();
		maxMeasurementInput[i] = Constraints[i].GetFixedMaxInput();
		variableMinMeasurementInput[i] = Constraints[i].GetVariableMinInput();
		variableMaxMeasurementInput[i] = Constraints[i].GetVariableMaxInput();

		bool fixedMeasurementsSet = minMeasurementInput[i] != BodyMeasurement::InvalidValue &&
			maxMeasurementInput[i] != BodyMeasurement::InvalidValue;
		
		bool variableMeasurementsSet = variableMinMeasurementInput[i].first != BodyMeasurement::InvalidValue &&
			variableMinMeasurementInput[i].second != BodyMeasurement::InvalidValue &&
			variableMaxMeasurementInput[i].first != BodyMeasurement::InvalidValue &&
			variableMaxMeasurementInput[i].second != BodyMeasurement::InvalidValue;

		if (!fixedMeasurementsSet && !variableMeasurementsSet)
		{
			missingIndices.push_back(i);
		}
		else if (fixedMeasurementsSet && !variableMeasurementsSet)
		{
			variableMinMeasurementInput[i].first = minMeasurementInput[i];
			variableMinMeasurementInput[i].second = minMeasurementInput[i];
			variableMaxMeasurementInput[i].first = maxMeasurementInput[i];
			variableMaxMeasurementInput[i].second = maxMeasurementInput[i];
		}
		else if (!fixedMeasurementsSet && variableMeasurementsSet)
		{
			minMeasurementInput[i] = variableMinMeasurementInput[i].first;
			maxMeasurementInput[i] = variableMaxMeasurementInput[i].second;
		}
	}

	if (missingIndices.empty())
	{
		return;
	}

	const auto GetMeasurements = [&](const Eigen::VectorXf& pose)
	{
		const DiffData<float> rawControls = rigLogic->EvaluateRawControls(pose);
		const DiffData<float> joints = rigLogic->EvaluateJoints(0, rawControls);
		BodyGeometry<float>::State state;
		rigGeometry->EvaluateBodyGeometry(0, joints, rawControls, state);
		return BodyMeasurement::GetBodyMeasurements(State.m->Constraints, state.Vertices().Matrix(), rawControls.Value());
	};

	Eigen::VectorXf minVals = Eigen::VectorXf::Constant(State.m->Constraints.size(), 1000000.0f);
	Eigen::VectorXf maxVals = Eigen::VectorXf::Constant(State.m->Constraints.size(), -1000000.0f);
	Eigen::VectorXf pose = Eigen::VectorXf::Zero(rigLogic->NumGUIControls());
	const float rawControlRange = 5.f;

	for (size_t i = 0; i < globalIndices.size(); i++)
	{
	    pose.setZero();
		pose[globalIndices[i]] = rawControlRange;
		Eigen::VectorXf measurements = GetMeasurements(pose);
		minVals = minVals.cwiseMin(measurements);
		maxVals = maxVals.cwiseMax(measurements);
		pose[globalIndices[i]] = -rawControlRange;
		measurements = GetMeasurements(pose);
		minVals = minVals.cwiseMin(measurements);
		maxVals = maxVals.cwiseMax(measurements);
	}

	for (int i : missingIndices)
	{
		if (State.m->Constraints[i].GetType() == BodyMeasurement::type_t::Semantic)
		{
			minVals[i] *= 1.5f;
			maxVals[i] *= 1.5f;
		}
        minMeasurementInput[i] = minVals[i];
        maxMeasurementInput[i] = maxVals[i];
		variableMinMeasurementInput[i] = {minVals[i], minVals[i]};
		variableMaxMeasurementInput[i] = {maxVals[i], maxVals[i]};
	}
}

void BodyShapeEditor::EvaluateConstraintRange(const State& State, av::ArrayView<float> MinValues, av::ArrayView<float> MaxValues, bool bScaleWithHeight) const
{
	const auto& constraints = State.m->Constraints;
	if ((MinValues.size() != MaxValues.size()) || (MinValues.size() != constraints.size()))
	{
		CARBON_CRITICAL("Output values buffer is not of right size");
	}
	
	if (m->variableMinMeasurementInput.empty() || m->variableMaxMeasurementInput.empty() || m->minMeasurementInput.empty() || m->maxMeasurementInput.empty())
	{
		m->SetMinMaxMeasurements(State);
	}

	if (bScaleWithHeight)
	{
		if (m->heightConstraintIndex < 0)
		{
			for (int i = 0; i < constraints.size(); i++)
			{
				const BodyMeasurement& constraint = constraints[i];
				if (constraint.GetName() == "Height" || constraint.GetName() == "height")
				{
					m->heightConstraintIndex = i;
					break;
				}
			}
		}

		if (m->heightConstraintIndex < 0 || m->heightConstraintIndex >= constraints.size())
		{
			CARBON_CRITICAL("Constraints must include a valid Height constraint");
		}

		// Get height constraint range and height measurement. Calculate height ratio to lerp ranges with.
		const auto& measurements = State.GetNamedConstraintMeasurements();

        float height = 177.5f;
        // Get height from constraint target if available, otherwise from measurement 
        if (!State.GetConstraintTarget(m->heightConstraintIndex, height)) 
        {
            height = measurements[m->heightConstraintIndex];
        }
		
		float min = m->variableMinMeasurementInput[m->heightConstraintIndex].first;
		float max = m->variableMaxMeasurementInput[m->heightConstraintIndex].second;
		if (max <= min)
		{
			CARBON_CRITICAL("Height constraint invalid. Max is less than or equal to Min ");
		}
		float heightRatio = (height - min) / (max - min);
		heightRatio = std::clamp(heightRatio, 0.f, 1.f);

		for (int i = 0; i < m->variableMinMeasurementInput.size(); ++i)
		{
			MinValues[i] = std::lerp(m->variableMinMeasurementInput[i].first, m->variableMinMeasurementInput[i].second, heightRatio);
			MaxValues[i] = std::lerp(m->variableMaxMeasurementInput[i].first, m->variableMaxMeasurementInput[i].second, heightRatio);
		}
	}
	else
	{
		std::copy_n(m->minMeasurementInput.data(), m->minMeasurementInput.size(), MinValues.data());
		std::copy_n(m->maxMeasurementInput.data(), m->maxMeasurementInput.size(), MaxValues.data());
	}
}

std::shared_ptr<BodyShapeEditor::State> BodyShapeEditor::RestoreState(trio::BoundedIOStream* InputStream)
{
    auto state = std::shared_ptr<State>(new State());
    state->m->GuiControls = Eigen::VectorX<float>::Zero(m->rigLogic->NumGUIControls());
    state->m->GuiControlsPrior = Eigen::VectorX<float>::Zero(m->rigLogic->NumGUIControls());
    state->m->RawControls = Eigen::VectorX<float>::Zero(m->rigLogic->NumRawControls());
    state->m->Constraints = m->Constraints;
    state->m->JointBindMatrices = m->rigGeometry->GetBindMatrices();
    state->m->JointDeltas = Eigen::Matrix<float, 3, -1>::Zero(3, m->rigGeometry->NumJoints());
    state->m->CustomPose = Eigen::VectorXf::Zero(m->rigGeometry->NumJoints());
    bool success = true;
    MHCBinaryInputArchive archive(InputStream);

    int32_t MagicNumber = { -1 };
    int32_t Version = { -1 };
    archive(MagicNumber);
    archive(Version);
    if (MagicNumber != m->MagicNumber)
    {
        LOG_ERROR("stream does not contain a MHC body state");
        success = false;
    }
    if ((Version < 1) || (Version > 8))
    {
        LOG_ERROR("version {} is not supported", Version);
        success = false;
    }

    SparseMatrix<float> PreviousSkinning;
    if (success)
    {
        if (Version > 3)
        {
            archive(state->m->ModelVersion);
        }
        else
        {
            state->m->ModelVersion = "0.4.4";
        }
        DeserializeEigenMatrix(archive, InputStream, state->m->GuiControls);
        if (state->m->GuiControls.size() != m->rigLogic->NumGUIControls())
        {
            state->m->GuiControls.setZero(m->rigLogic->NumGUIControls());
        }
        state->m->GuiControlsPrior = state->m->GuiControls;

        // Target Measurements
        if (Version > 3)
        {
            std::size_t targetMeasurementsSize;
            archive(targetMeasurementsSize);
            state->m->TargetMeasurements.reserve(targetMeasurementsSize);
            for (std::size_t i = 0; i < targetMeasurementsSize; i++)
            {
                std::string targetName;
                float targetValue;

                archive(targetName);
                targetName = BodyMeasurement::GetAlias(targetName);
                archive(targetValue);
                auto constraintIter = std::find_if(m->Constraints.begin(), m->Constraints.end(), [&targetName](const auto& Constraint)
                    { return Constraint.GetName() == targetName; });
                if (constraintIter != m->Constraints.end())
                {
                    state->m->TargetMeasurements.push_back({ static_cast<int>(std::distance(m->Constraints.begin(), constraintIter)), targetValue });
                }
            }
        }
        else
        {
            archive(state->m->TargetMeasurements);
            // We cant be sure if indices will be respected in newer version of model, hopefully by then everyone will migrate to 0.4.5
            if ((m->ModelVersion != "0.4.5") && (m->ModelVersion != "0.4.6"))
            {
                state->m->TargetMeasurements.clear();
            }
        }

        DeserializeEigenMatrix(archive, InputStream, state->m->VertexDeltas);
        if (Version > 4)
        {
            DeserializeEigenMatrix(archive, InputStream, state->m->JointDeltas);
            if (state->m->JointDeltas.cols()==0) 
            {
                state->m->JointDeltas = Eigen::Matrix<float, 3, -1>::Zero(3, m->rigGeometry->NumJoints());
            }
            if (Version > 5){
                DeserializeEigenMatrix(archive, InputStream, state->m->CustomPose);
            }
            if(state->m->CustomPose.rows() == 0)
            {
                state->m->CustomPose = Eigen::VectorXf::Zero(m->rigGeometry->NumJoints());
            }
        }
        Eigen::Matrix<float, 3, -1> vertices;
        DeserializeEigenMatrix(archive, InputStream, vertices);
        Eigen::Matrix<float, 3, -1> jointPositions;
        if (Version > 1)
        {
            DeserializeEigenMatrix(archive, InputStream, jointPositions);
            for (size_t i = 0; i < std::min<int>((int)state->m->JointBindMatrices.size(), (int)jointPositions.cols()); ++i)
            {
                state->m->JointBindMatrices[i].translation() = jointPositions.col(i);
            }
        }

        if (Version > 7 )
        {
            Eigen::Matrix<float, 3 ,-1> allLinear;
            DeserializeEigenMatrix(archive, InputStream, allLinear);
            for (size_t i = 0; i < std::min<int>((int)state->m->JointBindMatrices.size(), (int)allLinear.cols()); ++i)
            {
                state->m->JointBindMatrices[i].linear() = allLinear.block<3,3>(0, i * 3);
            }
        }

        if (Version > 2 && Version < 7) 
        {
            Eigen::VectorXf translation;
            DeserializeEigenMatrix(archive, InputStream, translation);
            if (translation.squaredNorm() > 0.0f)
            {
                state->m->CustomPose({0,1,2}) = translation;
            }
        }

        if (Version > 4)
        {
            archive(state->m->VertexDeltaScale);
            archive(state->m->FloorOffsetApplied);
        }

        // We need to update the state to new model version
        if (state->m->ModelVersion != m->ModelVersion)
        {
            FitToTargetOptions options;
            auto previousPCA = state->m->GuiControls;
            SolveForTemplateMesh(*state, vertices, options);
            SetNeutralMesh(*state, vertices);
            SetNeutralJointsTranslations(*state, jointPositions);
            if(m->skinningModels.contains(state->m->ModelVersion))
            {
                PreviousSkinning = m->skinningModels[state->m->ModelVersion].calculateResult(previousPCA(m->globalIndices));
            }
            const auto& newMesurements = state->GetNamedConstraintMeasurements();
            for (auto& [k, v] : state->m->TargetMeasurements)
            {
                v = newMesurements[k];
            }
        }
    }

    EvaluateState(*state);
    state->m->CustomSkinning = PreviousSkinning; 
    return state;
}

void BodyShapeEditor::DumpState(const State& state, trio::BoundedIOStream* OutputStream)
{
    MHCBinaryOutputArchive archive { OutputStream };

    int32_t Version = 8;
    archive(m->MagicNumber);
    archive(Version);
    archive(m->ModelVersion);
    // archive gui controls
    SerializeEigenMatrix(archive, OutputStream, state.m->GuiControls);

    // archive the target measurements
    archive(state.m->TargetMeasurements.size());
    for (const auto& [k, v] : state.m->TargetMeasurements)
    {
        archive(m->Constraints[k].GetName());
        archive(v);
    }
    // archive the vertex deltas
    SerializeEigenMatrix(archive, OutputStream, state.m->VertexDeltas);
    SerializeEigenMatrix(archive, OutputStream, state.m->JointDeltas);
    SerializeEigenMatrix(archive, OutputStream, state.m->CustomPose);
    // archive the actual body vertices (in order to be able to reconstruct the body in a future release)
    SerializeEigenMatrix(archive, OutputStream, state.m->RigMeshes[0].Vertices());
    // archive the joint positions
    Eigen::Matrix<float, 3, -1> jointPositions(3, (int)state.m->JointBindMatrices.size());
    Eigen::Matrix<float, 3, -1> allLinear(3, 3 * (int)state.m->JointBindMatrices.size());
    for (size_t i = 0; i < state.m->JointBindMatrices.size(); ++i)
    {
        jointPositions.col(i) = state.m->JointBindMatrices[i].translation();
        allLinear.block<3,3>(0, i * 3) = state.m->JointBindMatrices[i].linear();
    }
    SerializeEigenMatrix(archive, OutputStream, jointPositions);
    SerializeEigenMatrix(archive, OutputStream, allLinear);
    archive(state.m->VertexDeltaScale);
    archive(state.m->FloorOffsetApplied);
}

template <typename T>
std::pair<T, Eigen::RowVectorX<T>> EvaluateLength(const Eigen::Matrix<T, 3, -1>& vertices,
    const Eigen::SparseMatrix<T, Eigen::RowMajor>& evaluationMatrix,
    const std::vector<BarycentricCoordinates<T>>& lines)
{
    T length = 0;
    Eigen::RowVectorX<T> jacobian = Eigen::RowVectorX<T>::Zero(evaluationMatrix.cols());

    for (int j = 0; j < (int)lines.size() - 1; j++)
    {
        const BarycentricCoordinates<float>& b0 = lines[j];
        const BarycentricCoordinates<float>& b1 = lines[j + 1];
        const Eigen::Vector3<T> segment = b1.template Evaluate<3>(vertices) - b0.template Evaluate<3>(vertices);
        const T segmentLength = segment.norm();
        const T segmentWeight = segmentLength > T(1e-9f) ? T(1) / segmentLength : T(0);
        length += segmentLength;

        for (int d = 0; d < 3; d++)
        {
            T b0w = (T)b0.Weight(d);
            T b1w = (T)b1.Weight(d);
            jacobian += (-segmentWeight * segment[0] * b0w) * evaluationMatrix.row(3 * b0.Index(d) + 0);
            jacobian += (-segmentWeight * segment[1] * b0w) * evaluationMatrix.row(3 * b0.Index(d) + 1);
            jacobian += (-segmentWeight * segment[2] * b0w) * evaluationMatrix.row(3 * b0.Index(d) + 2);
            jacobian += (segmentWeight * segment[0] * b1w) * evaluationMatrix.row(3 * b1.Index(d) + 0);
            jacobian += (segmentWeight * segment[1] * b1w) * evaluationMatrix.row(3 * b1.Index(d) + 1);
            jacobian += (segmentWeight * segment[2] * b1w) * evaluationMatrix.row(3 * b1.Index(d) + 2);
        }
    }

    return { length, jacobian };
}

template <typename T>
std::pair<T, Eigen::RowVectorX<T>> EvaluateDistance(const Eigen::Matrix<T, 3, -1>& vertices,
    const Eigen::SparseMatrix<T, Eigen::RowMajor>& evaluationMatrix,
    int vID1,
    int vID2)
{
    Eigen::RowVectorX<T> jacobian = evaluationMatrix.row(3 * vID2 + 1) - evaluationMatrix.row(3 * vID1 + 1);
    return { vertices(1, vID2) - vertices(1, vID1), jacobian };
}

template <typename T>
std::pair<T, Eigen::RowVectorX<T>> EvaluateSemantic(const Eigen::VectorX<T>& rawControls, const Eigen::VectorX<T>& weights)
{
    Eigen::RowVectorX<T> jacobian = weights.transpose();
    return { rawControls.head(weights.size()).dot(weights), jacobian };
}

std::vector<int> GetUsedVertexIndices(int numVertices, const std::vector<BodyMeasurement>& measurements)
{
    std::vector<int> indices;
    indices.reserve(numVertices);

    std::vector<bool> used(numVertices, false);
    for (size_t i = 0; i < measurements.size(); i++)
    {
        for (const auto& b : measurements[i].GetBarycentricCoordinates())
        {
            used[b.Index(0)] = true;
            used[b.Index(1)] = true;
            used[b.Index(2)] = true;
        }
        for (size_t j = 0; j < measurements[i].GetVertexIDs().size(); ++j)
        {
            used[measurements[i].GetVertexIDs()[j]] = true;
        }
    }
    for (int vID = 0; vID < (int)used.size(); ++vID)
    {
        if (used[vID])
        {
            indices.push_back(vID);
        }
    }

    return indices;
}

void BodyShapeEditor::Solve(State& State, float priorWeight, const int /*iterations*/) const
{
    const int numVertices = m->rigGeometry->GetMesh(0).NumVertices();
    // list of vertices that's being controlled
    const std::vector<int> indices = GetUsedVertexIndices(numVertices, State.m->Constraints);

    Eigen::VectorXf symControls = m->symControls->GuiToSymmetricControls(State.m->GuiControls);
    const auto& symToGuiMat = m->symControls->SymmetricToGuiControlsMatrix();
    const auto& guiToRawMat = m->guiToRawMappingMatrix;
    Eigen::SparseMatrix<float, Eigen::RowMajor> symToRawMat = guiToRawMat * symToGuiMat;
    const Eigen::SparseMatrix<float, Eigen::RowMajor>& symEvalMat = m->symmetricIdentityVertexEvaluationMatrix;
    Eigen::Matrix<float, 3, -1> currVertices = m->rigGeometry->GetMesh(0).Vertices();

    Eigen::MatrixXf AtA(symControls.size(), symControls.size());
    Eigen::VectorXf Atb(symControls.size());

    const int numIterationsSteps = 2;
    std::set<std::string> involvedConstraintNames = {};
    for (const auto& [constraintNames, affectedRegions] : m->solveSteps)
    {
        involvedConstraintNames.insert(constraintNames.begin(), constraintNames.end());
        std::vector<bool> usedSymmetricControls(symControls.size(), false);
        int affected = 0;
        for (const std::string& regionName : affectedRegions)
        {
            bool found = false;
            auto it = m->shapePcaControls.find(regionName);
            if (it != m->shapePcaControls.end())
            {
                found = true;
                for (int rawControl : it->second)
                {
                    int guiControl = m->rawToGuiControls[rawControl];
                    for (Eigen::SparseMatrix<float, Eigen::RowMajor>::InnerIterator itMat(symToGuiMat, guiControl); itMat; ++itMat)
                    {
                        usedSymmetricControls[itMat.col()] = true;
                        affected++;
                    }
                }
            }
            it = m->skeletonPcaControls.find(regionName);
            if (it != m->skeletonPcaControls.end())
            {
                found = true;
                for (int rawControl : it->second)
                {
                    int guiControl = m->rawToGuiControls[rawControl];
                    for (Eigen::SparseMatrix<float, Eigen::RowMajor>::InnerIterator itMat(symToGuiMat, guiControl); itMat; ++itMat)
                    {
                        usedSymmetricControls[itMat.col()] = true;
                        affected++;
                    }
                }
            }
            if (!found)
            {
                for (int guiControl = 0; guiControl < symToGuiMat.outerSize(); ++guiControl)
                {
                    for (Eigen::SparseMatrix<float, Eigen::RowMajor>::InnerIterator itMat(symToGuiMat, guiControl); itMat; ++itMat)
                    {
                        usedSymmetricControls[itMat.col()] = true;
                        affected++;
                    }
                }
            }
        }

        for (int iter = 0; iter < numIterationsSteps; ++iter)
        {
            Eigen::VectorXf guiControls = symToGuiMat * symControls;
            Eigen::VectorXf rawControls = guiToRawMat * guiControls;

            AtA.setZero();
            Atb.setZero();

            // prior cost
            if (priorWeight > 0)
            {
                if (State.m->GuiControlsPrior.size() != guiControls.size())
                {
                    State.m->GuiControlsPrior = Eigen::VectorXf::Zero(guiControls.size());
                }
                Eigen::SparseMatrix<float, Eigen::RowMajor> gwmFullMat = m->gwm * symToGuiMat;
                AtA += priorWeight * (gwmFullMat.transpose() * gwmFullMat);
                Atb += priorWeight * (gwmFullMat.transpose() * m->gwm * (State.m->GuiControlsPrior - symToGuiMat * symControls));
            }

            for (int vID : indices)
            {
                currVertices(0, vID) = m->rigGeometry->GetMesh(0).Vertices()(0, vID) + symEvalMat.row(3 * vID + 0) * symControls;
                currVertices(1, vID) = m->rigGeometry->GetMesh(0).Vertices()(1, vID) + symEvalMat.row(3 * vID + 1) * symControls;
                currVertices(2, vID) = m->rigGeometry->GetMesh(0).Vertices()(2, vID) + symEvalMat.row(3 * vID + 2) * symControls;
            }

            for (const auto& keyValuePair : State.m->TargetMeasurements)
            {
                int ConstraintIndex = keyValuePair.first;
                float ConstraintWeight = keyValuePair.second;
                if (involvedConstraintNames.count(State.m->Constraints[ConstraintIndex].GetName()) > 0)
                {
                    if (State.m->Constraints[ConstraintIndex].GetType() == BodyMeasurement::Axis)
                    {
                        // axis costs
                        auto [dist, jacobian] = EvaluateDistance<float>(currVertices,
                            symEvalMat,
                            State.m->Constraints[ConstraintIndex].GetVertexIDs()[0],
                            State.m->Constraints[ConstraintIndex].GetVertexIDs()[1]);
                        AtA.template triangularView<Eigen::Lower>() += jacobian.transpose() * jacobian;
                        Atb += jacobian.transpose() * (ConstraintWeight - dist);
                    }
                    else if (State.m->Constraints[ConstraintIndex].GetType() == BodyMeasurement::Semantic)
                    {
                        // semantic costs
                        auto [value, jacobian] = EvaluateSemantic<float>(rawControls, State.m->Constraints[ConstraintIndex].GetWeights());
                        // jacobian of raw controls, convert to sym
                        Eigen::RowVectorXf symJacobian = jacobian * guiToRawMat * symToGuiMat;
                        float diff = ConstraintWeight - value;
                        AtA.template triangularView<Eigen::Lower>() += State.m->SemanticWeight * (symJacobian.transpose() * symJacobian);
                        Atb += State.m->SemanticWeight * (symJacobian.transpose() * diff);
                    }
                    else
                    {
                        // length costs
                        auto [value, jacobian] = EvaluateLength(currVertices, symEvalMat, State.m->Constraints[ConstraintIndex].GetBarycentricCoordinates());
                        float diff = ConstraintWeight - value;
                        AtA.template triangularView<Eigen::Lower>() += (jacobian.transpose() * jacobian);
                        Atb += (jacobian.transpose() * diff);
                    }
                }
            }

            for (int i = 0; i < (int)usedSymmetricControls.size(); ++i)
            {
                if (!usedSymmetricControls[i])
                {
                    AtA.col(i).setZero();
                    AtA.row(i).setZero();
                    Atb(i) = 0;
                }
            }

            // regularization
            for (int i = 0; i < AtA.cols(); ++i)
            {
                AtA(i, i) += 1e-2f;
            }

            // solve
            const Eigen::VectorXf dx = AtA.template selfadjointView<Eigen::Lower>().llt().solve(Atb);
            symControls += dx;
        }
    }

    State.m->GuiControls = symToGuiMat * symControls;
    State.m->RawControls = m->rigLogic->EvaluateRawControls(DiffData<float>(State.m->GuiControls)).Value();
    const auto& rawControls = State.m->RawControls;
    const float rawMean = rawControls.mean();
    const float rawStdDev = std::sqrt((rawControls.array() - rawMean).square().sum() / rawControls.size());
    for (int i = 0; i < State.m->RawControls.size(); ++i)
    {
        const auto& rawControlName = m->rigLogic->RawControlNames()[i];
        if (rawControlName.starts_with("local_groin") || rawControlName.starts_with("local_pelvis_0"))
        {
            auto& groinControl = State.m->RawControls[i];
            if (groinControl < rawMean - 2 * rawStdDev)
            {
                groinControl = rawMean - 2 * rawStdDev;
            }
            else if (groinControl > rawMean + 2 * rawStdDev)
            {
                groinControl = rawMean + 2 * rawStdDev;
            }
        }
    }
    UpdateGuiFromRawControls(State);
    EvaluateState(State);
}

void BodyShapeEditor::StateToDna(const State& State, dna::Writer* InOutDnaWriter, bool combinedBodyAndFace) const
{
    std::vector<SparseMatrix<float>> vertexInfluenceWeights;
    GetVertexInfluenceWeights(State, vertexInfluenceWeights);
    if (vertexInfluenceWeights.size() > 0)
    {
        // update the skinning weights in the DNA
        for (int lod = 0; lod < NumLODs(); ++lod)
        {
            InOutDnaWriter->clearSkinWeights((uint16_t)lod);
            int numVertices = static_cast<int>(vertexInfluenceWeights[size_t(lod)].rows());
            if (combinedBodyAndFace)
            {
                // write skin weights for the combined face and body
                for (int vID = numVertices - 1; vID >= 0; --vID)
                {
                    std::vector<float> weights;
                    std::vector<uint16_t> indices;
                    for (typename SparseMatrix<float>::InnerIterator it(vertexInfluenceWeights[size_t(lod)], vID); it; ++it)
                    {
                        if (it.value() != float(0))
                        {
                            weights.push_back(it.value());
                            indices.emplace_back((uint16_t)it.col());
                        }
                    }

                    InOutDnaWriter->setSkinWeightsValues(lod, vID, weights.data(), (uint16_t)weights.size());
                    InOutDnaWriter->setSkinWeightsJointIndices(lod, vID, indices.data(), (uint16_t)indices.size());
                }
            }
            else
            {
                // map weights from combined to headless body
                for (int vID = numVertices - 1; vID >= 0; --vID)
                {
                    if (m->combinedToBodyMapping[lod].find(vID) != m->combinedToBodyMapping[lod].end())
                    {
                        int bodyVID = m->combinedToBodyMapping[lod][vID];
                        std::vector<float> weights;
                        std::vector<uint16_t> indices;

                        for (typename SparseMatrix<float>::InnerIterator it(vertexInfluenceWeights[size_t(lod)], vID); it; ++it)
                        {
                            if (it.value() != float(0))
                            {
                                weights.push_back(it.value());
                                indices.emplace_back((uint16_t)it.col());
                            }
                        }

                        InOutDnaWriter->setSkinWeightsValues(lod, bodyVID, weights.data(), (uint16_t)weights.size());
                        InOutDnaWriter->setSkinWeightsJointIndices(lod, bodyVID, indices.data(), (uint16_t)indices.size());
                    }
                }
            }
        }
    }


    for (int lod = 0; lod < NumLODs(); ++lod)
    {
        const uint16_t meshIndex = static_cast<uint16_t>(lod);
        if (combinedBodyAndFace)
        {
            const Eigen::Matrix3Xf& bodyVertices = State.GetMesh(lod).Vertices();
            const Eigen::Matrix3Xf& bodyNormals = State.GetMesh(lod).VertexNormals();
            InOutDnaWriter->setVertexPositions(meshIndex, (const dna::Position*)bodyVertices.data(), uint32_t(bodyVertices.cols()));
            InOutDnaWriter->setVertexNormals(meshIndex, (const dna::Position*)bodyNormals.data(), uint32_t(bodyNormals.cols()));
        }
        else
        {
            const std::vector<int>& bodyToCombinedMapping = GetBodyToCombinedMapping(static_cast<int>(lod));
            const Eigen::Matrix3Xf bodyVertices = State.GetMesh(lod).Vertices()(Eigen::all, bodyToCombinedMapping);
            const Eigen::Matrix3Xf bodyNormals = State.GetMesh(lod).VertexNormals()(Eigen::all, bodyToCombinedMapping);
            InOutDnaWriter->setVertexPositions(meshIndex, (const dna::Position*)bodyVertices.data(), uint32_t(bodyVertices.cols()));
            InOutDnaWriter->setVertexNormals(meshIndex, (const dna::Position*)bodyNormals.data(), uint32_t(bodyNormals.cols()));
        }
    }

    // Set neutral joints
    constexpr float rad2deg = float(180.0 / CARBON_PI);

    uint16_t numJoints = m->rigGeometry->NumJoints();

    Eigen::Matrix<float, 3, -1> jointTranslations(3, numJoints);
    Eigen::Matrix<float, 3, -1> jointRotations(3, numJoints);

    const auto& jointHierarchy = m->rigGeometry->GetJointParentIndices();

    const auto getJointParent = [&jointHierarchy](std::uint16_t jointIndex)
    {
        return jointHierarchy[jointIndex];
    };

    const std::vector<Eigen::Transform<float, 3, Eigen::Affine>>& jointMatrices = State.GetJointBindMatrices();

    for (std::uint16_t jointIndex = 0; jointIndex < numJoints; jointIndex++)
    {
        Eigen::Transform<float, 3, Eigen::Affine> localTransform;
        const int parentJointIndex = getJointParent(jointIndex);
        if (parentJointIndex >= 0)
        {
            auto parentTransform = jointMatrices[parentJointIndex];
            localTransform = parentTransform.inverse() * jointMatrices[jointIndex];
        }
        else
        {
            localTransform = jointMatrices[jointIndex];
        }

        jointTranslations.col(jointIndex) = localTransform.translation();
        jointRotations.col(jointIndex) = rad2deg * RotationMatrixToEulerXYZ<float>(localTransform.linear());
    }

    InOutDnaWriter->setNeutralJointTranslations((dna::Vector3*)jointTranslations.data(), numJoints);
    InOutDnaWriter->setNeutralJointRotations((dna::Vector3*)jointRotations.data(), numJoints);

    if (m->rbfPCA.mods.size() > 0)
    {
        auto rbfMatrix = m->rbfPCA.calculateResult(State.m->GuiControls(m->globalIndices));
        for (std::uint16_t jg = 0; jg < m->jointGroupInputIndices.size(); ++jg)
        {
            const auto& inputIndices = m->jointGroupInputIndices[jg];
            const auto& outputIndices = m->jointGroupOutputIndices[jg];
            std::vector<float> values;
            values.reserve(inputIndices.size() * outputIndices.size());
            for (const auto oi : outputIndices)
            {
                float multiplier = 1.0f;
                if (oi % 9 > 2 && oi % 9 < 6){
                    multiplier = rad2deg;
                }
                for (const auto ii : inputIndices)
                {
                    values.push_back(rbfMatrix.coeff(oi, ii - m->rbfControlOffset) * multiplier);
                }
            }
            InOutDnaWriter->setJointGroupValues(jg, values.data(), static_cast<std::uint32_t>(values.size()));
        }
    }
}

int BodyShapeEditor::NumJoints() const
{
    return m->rigGeometry->NumJoints();
}

void BodyShapeEditor::GetNeutralJointTransform(const State& State,
    std::uint16_t JointIndex,
    Eigen::Vector3f& OutJointTranslation,
    Eigen::Vector3f& OutJointRotation) const
{
    constexpr float rad2deg = float(180.0 / CARBON_PI);

    if (JointIndex >= m->rigGeometry->NumJoints())
    {
        CARBON_CRITICAL("JointIndex out of range");
    }

    const auto& jointHierarchy = m->rigGeometry->GetJointParentIndices();

    const std::vector<Eigen::Transform<float, 3, Eigen::Affine>>& jointMatrices = State.GetJointBindMatrices();

    Eigen::Transform<float, 3, Eigen::Affine> localTransform;
    const int parentJointIndex = jointHierarchy[JointIndex];
    if (parentJointIndex >= 0)
    {
        auto parentTransform = jointMatrices[parentJointIndex];
        localTransform = parentTransform.inverse() * jointMatrices[JointIndex];
    }
    else
    {
        localTransform = jointMatrices[JointIndex];
    }

    OutJointTranslation = localTransform.translation();
    OutJointRotation = rad2deg * RotationMatrixToEulerXYZ<float>(localTransform.linear());
}

Eigen::Matrix<float, 3, -1> ScatterCol(const Eigen::Matrix<float, 3, -1>& input, const std::vector<int>& ids, int numCols)
{
    Eigen::Matrix<float, 3, -1> target = Eigen::Matrix<float, 3, -1>::Zero(3, numCols);
    for (int i = 0; i < (int)ids.size(); ++i)
    {
        target.col(ids[i]) = input.col(i);
    }
    return target;
}


void BodyShapeEditor::SetCustomGeometryToState(State& State, std::shared_ptr<const BodyGeometry<float>> Geometry, bool Fit)
{
    if (!Geometry)
    {
        return;
    }

    if (Fit)
    {
        FitToTargetOptions fitToTargetOptions;

        const int numVertices = Geometry->GetMesh(0).NumVertices();
        std::vector<int> mapping(numVertices);
        std::iota(mapping.begin(), mapping.end(), 0);
        Eigen::Matrix3Xf InJoints = Eigen::Matrix3Xf::Zero(3, Geometry->NumJoints());
        for (int i = 0; i < Geometry->NumJoints(); ++i)
        {
            InJoints.col(i) = Geometry->GetBindMatrices()[i].translation();
        }
        SolveForTemplateMesh(State, Geometry->GetMesh(0).Vertices(),fitToTargetOptions);
        SetNeutralJointsTranslations(State, InJoints);
    }
    else
    {
        State.m->RigMeshes.resize(Geometry->GetNumLODs());
        for (int lod = 0; lod < Geometry->GetNumLODs(); ++lod)
        {
            State.m->RigMeshes[lod].SetTriangles(m->meshTriangles[lod]);
            State.m->RigMeshes[lod].SetVertices(Geometry->GetMesh(lod).Vertices());
            State.m->RigMeshes[lod].CalculateVertexNormals(false, VertexNormalComputationType::AreaWeighted, /*stableNormalize=*/true, m->threadPool.get());
        }
        State.m->JointBindMatrices = Geometry->GetBindMatrices();
    }
}

const std::vector<std::string>& BodyShapeEditor::GetRegionNames() const
{
    return m->regionNames;
}

bool BodyShapeEditor::Blend(State& state, int RegionIndex, const std::vector<std::pair<float, const State*>>& States, BodyAttribute Type)
{
    const int numRegions = (int)m->regionNames.size();
    Eigen::VectorXf rawControls = state.m->RawControls;
    Eigen::Matrix3Xf VertexDeltas = state.m->VertexDeltas;
    Eigen::Matrix3Xf JointDeltas = state.m->JointDeltas;
    if (VertexDeltas.size() == 0)
    {
        VertexDeltas = Eigen::Matrix3Xf::Zero(3, m->rigGeometry->GetMesh(0).NumVertices());
    }
    if (JointDeltas.size() == 0)
    {
        JointDeltas = Eigen::Matrix3Xf::Zero(3, m->rigGeometry->NumJoints());
    }

    auto BlendRegion = [&](int idx)
    {
        if ((idx < 0) || (idx >= (int)m->regionNames.size()))
        {
            return;
        }
        for (const auto& [alpha, otherState] : States)
        {
            if ((Type == BodyAttribute::Skeleton) || (Type == BodyAttribute::Both))
            {
                auto it = m->skeletonPcaControls.find("joint_" + m->regionNames[idx]);
                if (it != m->skeletonPcaControls.end())
                {
                    rawControls(it->second) += alpha * (otherState->m->RawControls(it->second) - rawControls(it->second));
                }
                if ((otherState->m->JointDeltas.size() > 0) || (state.m->JointDeltas.size() > 0))
                {
                    for (int ji : m->regionToJoints[m->regionNames[idx]])
                    {
                        if ((ji < otherState->m->JointDeltas.cols()) && (ji < state.m->JointDeltas.cols()))
                        {
                            JointDeltas.col(ji) += alpha * (otherState->m->VertexDeltaScale * otherState->m->JointDeltas.col(ji)) - state.m->JointDeltas.col(ji);
                        }
                        else if (ji < otherState->m->JointDeltas.cols())
                        {
                            JointDeltas.col(ji) += alpha * (otherState->m->VertexDeltaScale * otherState->m->JointDeltas.col(ji));
                        }
                        else
                        {
                            JointDeltas.col(ji) -= alpha * state.m->JointDeltas.col(ji);
                        }
                    }
                }
            }
            if ((Type == BodyAttribute::Shape) || (Type == BodyAttribute::Both))
            {
                auto it = m->shapePcaControls.find(m->regionNames[idx]);
                if (it != m->shapePcaControls.end())
                {
                    rawControls(it->second) += alpha * (otherState->m->RawControls(it->second) - rawControls(it->second));
                }

                if ((otherState->m->VertexDeltas.size() > 0) || (state.m->VertexDeltas.size() > 0))
                {
                    auto it2 = m->partWeights.find(m->regionNames[idx]);
                    if (it2 != m->partWeights.end())
                    {
                        for (const auto& [vID, weight] : it2->second.NonzeroVerticesAndWeights())
                        {
                            if ((vID < otherState->m->VertexDeltas.cols()) && (vID < state.m->VertexDeltas.cols()))
                            {
                                VertexDeltas.col(vID) += weight * alpha * ((otherState->m->VertexDeltaScale * otherState->m->VertexDeltas.col(vID)) - state.m->VertexDeltas.col(vID));
                            }
                            else if (vID < otherState->m->VertexDeltas.cols())
                            {
                                VertexDeltas.col(vID) += (weight * alpha * otherState->m->VertexDeltaScale) * (otherState->m->VertexDeltas.col(vID));
                            }
                            else
                            {
                                VertexDeltas.col(vID) -= weight * alpha * (state.m->VertexDeltas.col(vID));
                            }
                        }
                    }
                }
            }
        }
    };

    if ((RegionIndex < 0) || (RegionIndex >= numRegions))
    {
        for (int idx = 0; idx < numRegions; ++idx)
        {
            BlendRegion(idx);
        }
    }
    else
    {
        BlendRegion(RegionIndex);
        if (state.m->UseSymmetry)
        {
            auto it = m->symmetricPartMapping.find(m->regionNames[RegionIndex]);
            if (it != m->symmetricPartMapping.end())
            {
                const int SymmetricRegionIndex = GetItemIndex(m->regionNames, it->second);
                if ((SymmetricRegionIndex != RegionIndex) && (SymmetricRegionIndex >= 0))
                {
                    BlendRegion(SymmetricRegionIndex);
                }
            }
        }
    }

    state.m->RawControls = rawControls;
    if (VertexDeltas.squaredNorm() > 0)
    {
        state.m->VertexDeltas = VertexDeltas;
    }
    else
    {
        state.m->VertexDeltas.resize(3, 0);
    }

    UpdateGuiFromRawControls(state);
    EvaluateState(state);
    state.m->GuiControlsPrior = state.m->GuiControls;
    state.m->TargetMeasurements.clear();

    return true;
}

bool BodyShapeEditor::GetMeasurements(Eigen::Ref<const Eigen::Matrix<float, 3, -1>> combinedBodyAndFaceVertices, Eigen::VectorXf& measurements,
    std::vector<std::string>& measurementNames) const
{
    if ((int)combinedBodyAndFaceVertices.cols() != m->rigGeometry->GetMesh(0).NumVertices())
    {
        CARBON_CRITICAL("vertices have incorrect size for combined body and face: {}, but expected {}",
            combinedBodyAndFaceVertices.cols(),
            m->rigGeometry->GetMesh(0).NumVertices());
    }

    std::vector<BodyMeasurement> Constraints;
    measurementNames.clear();
    for (int i = 0; i < (int)m->Constraints.size(); ++i)
    {
        if (m->Constraints[i].GetType() != BodyMeasurement::type_t::Semantic)
        {
            Constraints.push_back(m->Constraints[i]);
            measurementNames.push_back(m->Constraints[i].GetName());
        }
    }
    Eigen::Matrix<float, 3, -1> vertexNormals;
    m->triTopology->CalculateVertexNormals(combinedBodyAndFaceVertices,
        vertexNormals,
        VertexNormalComputationType::AreaWeighted,
        /*stableNormalize*/ true,
        m->threadPool.get());
    BodyMeasurement::UpdateBodyMeasurementPoints(Constraints, combinedBodyAndFaceVertices, vertexNormals, *m->heTopology, m->threadPool.get());
    measurements = BodyMeasurement::GetBodyMeasurements(Constraints, combinedBodyAndFaceVertices, Eigen::VectorXf());

    return true;
}

bool BodyShapeEditor::GetMeasurements(Eigen::Ref<const Eigen::Matrix<float, 3, -1>> faceVertices,
    Eigen::Ref<const Eigen::Matrix<float, 3, -1>> bodyVertices,
    Eigen::VectorXf& measurements,
    std::vector<std::string>& measurementNames) const
{
    if (m->bodyToCombinedMapping.empty())
    {
        CARBON_CRITICAL("body to combined mapping is missing");
    }
    if ((int)bodyVertices.cols() != (int)m->bodyToCombinedMapping.front().size())
    {
        CARBON_CRITICAL("incorrect body vertices size: {}, but expected {}", bodyVertices.cols(), m->bodyToCombinedMapping.front().size());
    }
    Eigen::Matrix<float, 3, -1> combinedBodyAndFaceVertices = m->rigGeometry->GetMesh(0).Vertices();
    if (faceVertices.cols() > combinedBodyAndFaceVertices.cols())
    {
        CARBON_CRITICAL("invalid number of face vertices: {}", faceVertices.cols());
    }
    combinedBodyAndFaceVertices.leftCols(faceVertices.cols()) = faceVertices;
    combinedBodyAndFaceVertices(Eigen::all, m->bodyToCombinedMapping.front()) = bodyVertices;
    return GetMeasurements(combinedBodyAndFaceVertices, measurements, measurementNames);
}

SnapConfig<float> BodyShapeEditor::Private::CalcNeckSeamSkinningWeightsSnapConfig(int lod) const
{
    SnapConfig<float> curSnapConfig;
    curSnapConfig.sourceVertexIndices = neckSeamIndices[0];

    const Eigen::Matrix<float, 3, -1>& curLodMeshVertices = combinedBodyArchetypeRigGeometry->GetMesh(lod).Vertices();
    const Eigen::Matrix<float, 3, -1>& lod0MeshVertices = combinedBodyArchetypeRigGeometry->GetMesh(0).Vertices();

    // find closest vertex in lod N for each sourceVertexIndex, and ensure the distance is close to zero
    for (size_t sInd = 0; sInd < curSnapConfig.sourceVertexIndices.size(); ++sInd)
    {
        Eigen::Matrix<float, 3, 1> curSourceVert = lod0MeshVertices.col(curSnapConfig.sourceVertexIndices[sInd]);
        float closestDist2 = std::numeric_limits<float>::max();
        int closestVInd = 0;
        for (int tInd = 0; tInd < curLodMeshVertices.cols(); ++tInd)
        {
            float curDist2 = (curSourceVert - curLodMeshVertices.col(tInd)).squaredNorm();
            if (curDist2 < closestDist2)
            {
                closestDist2 = curDist2;
                closestVInd = tInd;
            }
        }

        curSnapConfig.targetVertexIndices.emplace_back(closestVInd);
    }

    return curSnapConfig;
}

void BodyShapeEditor::GetVertexInfluenceWeights(const State& state, std::vector<SparseMatrix<float>>& vertexInfluenceWeights) const
{
    if (m->skinWeightsPCA.mean.size() > 0u)
    {
        vertexInfluenceWeights.resize(size_t(NumLODs()));
        vertexInfluenceWeights.resize(NumLODs());
        if (state.m->CustomSkinning.size() > 0)
        {
            vertexInfluenceWeights[0] = state.m->CustomSkinning; 
        }
        else 
        {
            vertexInfluenceWeights[0] =  m->skinWeightsPCA.calculateResult(state.m->GuiControls(m->globalIndices));
        }
        skinningweightutils::SortPruneAndRenormalizeSkinningWeights(vertexInfluenceWeights[0], GetMaxSkinWeights()[0]);

        // now propagate the skinning weights to higher lods
        std::map<std::string, std::vector<BarycentricCoordinates<float>>> lod0MeshClosestPointBarycentricCoordinates;
        if (m->combinedLodGenerationData)
        {
            m->combinedLodGenerationData->GetDriverMeshClosestPointBarycentricCoordinates(lod0MeshClosestPointBarycentricCoordinates);
        }

        for (int lod = 1; lod < NumLODs(); ++lod)
        {
            std::vector<BarycentricCoordinates<float>> curLodBarycentricCoordinates = lod0MeshClosestPointBarycentricCoordinates.at(
                m->combinedLodGenerationData->HigherLodMeshNames()[lod - 1]);
            skinningweightutils::PropagateSkinningWeightsToHigherLOD<float>(curLodBarycentricCoordinates,
                m->combinedBodyArchetypeRigGeometry->GetMesh(0).Vertices(),
                vertexInfluenceWeights[0],
                m->jointSkinningWeightLodPropagationMap[lod - 1],
                m->skinningWeightSnapConfigs[size_t(lod - 1)],
                *m->combinedBodyArchetypeRigGeometry,
                GetMaxSkinWeights()[size_t(lod)],
                vertexInfluenceWeights[size_t(lod)]);
        }
    }
}

int BodyShapeEditor::GetJointIndex(const std::string& JointName) const
{
    return m->combinedBodyArchetypeRigGeometry->GetJointIndex(JointName);
}

CARBON_NAMESPACE_END(TITAN_NAMESPACE)

