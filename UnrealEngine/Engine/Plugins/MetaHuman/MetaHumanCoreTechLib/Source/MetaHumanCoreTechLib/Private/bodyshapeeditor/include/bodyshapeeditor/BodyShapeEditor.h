// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "bodyshapeeditor/BodyJointEstimator.h"
#include <carbon/utils/TaskThreadPool.h>
#include <bodyshapeeditor/BodyMeasurement.h>
#include <trio/Stream.h>
#include <carbon/common/Defs.h>
#include <arrayview/ArrayView.h>
#include <arrayview/StringView.h>
#include <rig/BodyLogic.h>
#include <rig/BodyGeometry.h>
#include <nls/geometry/Affine.h>
#include <rig/RigLogic.h>
#include <nls/geometry/Mesh.h>
#include <nls/geometry/LodGeneration.h>
#include <nrr/VertexWeights.h>
#include <dna/Reader.h>
#include <dna/Writer.h>

#include <memory>

CARBON_NAMESPACE_BEGIN(TITAN_NAMESPACE)

class BodyShapeEditor
{
public:
    class State;

    enum class BodyAttribute
    {
        Skeleton,
        Shape,
        Both
    };

public:
    ~BodyShapeEditor();
    BodyShapeEditor();

    BodyShapeEditor(const BodyShapeEditor& other) = delete;
    BodyShapeEditor(BodyShapeEditor&& other) = delete;
    BodyShapeEditor& operator=(const BodyShapeEditor& other) = delete;
    BodyShapeEditor& operator=(BodyShapeEditor&& other) = delete;

    void SetThreadPool(const std::shared_ptr<TaskThreadPool>& threadPool);

    void Init(const dna::Reader* reader,
              trio::BoundedIOStream* rbfModelStream,
              trio::BoundedIOStream* skinModelStream,
              dna::Reader* InCombinedBodyArchetypeDnaReader,
              const std::vector<std::map<std::string, std::map<std::string, float>>>& JointSkinningWeightLodPropagationMap,
              const std::vector<int>& maxSkinWeightsPerVertexForEachLod = { 12, 8, 8, 4},
              std::shared_ptr<const LodGeneration<float>> combinedLodGenerationData = nullptr);
    void Init(std::shared_ptr<BodyLogic<float>> BodyLogic,
              std::shared_ptr<BodyGeometry<float>> CombinedBodyArchetypeGeometry,
              std::shared_ptr<RigLogic<float>> CombinedBodyRigLogic,
              std::shared_ptr<BodyGeometry<float>> BodyGeometry,
              av::ConstArrayView<BodyMeasurement> contours,
              const std::vector<std::map<std::string, std::map<std::string, float>>>& JointSkinningWeightLodPropagationMap,
              const std::vector<int>& maxSkinWeightsPerVertexForEachLod = { 12, 8, 8, 4 },
              std::shared_ptr<const LodGeneration<float>> combinedLodGenerationData = nullptr,
              const std::map<std::string, VertexWeights<float>>& partWeights = {});

    void SetFittingVertexIDs(const std::vector<int>& vertexIds);

    void SetNeckSeamVertexIDs(const std::vector<std::vector<int>>& vertexIds);

    void SetBodyToCombinedMapping(int lod, const std::vector<int>& bodyToCombinedMapping);

    const std::vector<int>& GetBodyToCombinedMapping(int lod = 0) const;

    int NumLODs() const;

    void EvaluateConstraintRange(const State& state, av::ArrayView<float> MinValues, av::ArrayView<float> MaxValues, bool bScaleWithHeight) const;

    std::shared_ptr<State> CreateState() const;

    //! Evaluate the state and update the meshes
    void EvaluateState(State& State) const;

    std::shared_ptr<BodyLogic<float>> GetBodyLogic() const;

    //! Estimate Gui from Raw controls
    void UpdateGuiFromRawControls(State& state) const;

    std::shared_ptr<State> RestoreState(trio::BoundedIOStream* InputStream);
    void DumpState(const State& State, trio::BoundedIOStream* OutputStream);

    void Solve(State& State, float priorWeight = 1.0f, const int iterations = 2) const;

    struct FitToTargetOptions {
        bool enforceAnatomicalPose{false};
        bool solveForPose{true};
        int iterations = 20;
        float epsilon1 = 1e-3;
        float epsilon2 = 1e-3;
    };
    // void (Eigen::Matrix<float,3,-1>& vertices, int iteration, float cost, std::vector<Eigen::Transform<float, 3, Eigen::Affine>> worldMatrices) 
    using IterationFunc = std::function<void(const Eigen::Matrix<float, 3, -1>&, int, float, std::vector<Eigen::Transform<float, 3, Eigen::Affine>>)>;
    Vector<float> SolveForTemplateMesh(State& State, const Eigen::Ref<const Eigen::Matrix<float, 3, -1>>& InVertices, const FitToTargetOptions& fitToTargetOptions, av::ConstArrayView<float> inJointRotations = {}, Eigen::VectorXf vertexWeightsOverride = {}, 
    IterationFunc func = [](const Eigen::Matrix<float, 3, -1>&,
                        int,
                        float,
                        std::vector<Eigen::Transform<float, 3, Eigen::Affine>>) {} );

    void SetNeutralJointRotations(State& state, av::ConstArrayView<float> inJointRotations);

    void VolumetricallyFitHandAndFeetJoints(State& state);

    void SetNeutralJointsTranslations(State& State, const Eigen::Ref<const Eigen::Matrix<float, 3, -1>>& InJoints);
    void SetNeutralMesh(State& State, const Eigen::Ref<const Eigen::Matrix<float, 3, -1>>& inMesh);

    const BodyJointEstimator& JointEstimator();

    void UpdateMeasurementPoints(State& State) const;

    void StateToDna(const State& State, dna::Writer* InOutDnaWriter, bool combinedBodyAndFace = false) const;

    int NumJoints() const;
    void GetNeutralJointTransform(const State& State, std::uint16_t JointIndex, Eigen::Vector3f& OutJointTranslation, Eigen::Vector3f& OutJointRotation) const;


    void SetCustomGeometryToState(State& state, std::shared_ptr<const BodyGeometry<float>> Geometry, bool Fit);

    //! calculate the skinning weights for the supplied body state at each lod; the body must have a skin weights pca present for this to work
    void GetVertexInfluenceWeights(const State& state, std::vector<SparseMatrix<float>>& vertexInfluenceWeights) const;

    //! get the maximum number of skin weights for each joint at each LOD of the combined body model (by default this is set to 12, 8, 8, 4)
    const std::vector<int>& GetMaxSkinWeights() const;
    //! set the maximum number of skin weights for each joint at each LOD of the combined body model
    void SetMaxSkinWeights(const std::vector<int>& MaxSkinWeights);

    int GetJointIndex(const std::string& JointName) const;

    //! @returns the region names
    const std::vector<std::string>& GetRegionNames() const;

    //! Blends the states
    bool Blend(State& state, int RegionIndex, const std::vector<std::pair<float, const State*>>& States, BodyAttribute Type);

    //! Calculate measurements on the combined body vertices
    bool GetMeasurements(Eigen::Ref<const Eigen::Matrix<float, 3, -1>> combinedBodyAndFaceVertices, Eigen::VectorXf& measurements, std::vector<std::string>& measurementNames) const;

    //! Calculate measurements on the body and face vertices
    bool GetMeasurements(Eigen::Ref<const Eigen::Matrix<float, 3, -1>> faceVertices, Eigen::Ref<const Eigen::Matrix<float, 3, -1>> bodyVertices, Eigen::VectorXf& measurements, std::vector<std::string>& measurementNames) const;

    //! Get the number of mesh vertices for LOD 0, either for the standalone body, or the combined body
    int GetNumLOD0MeshVertices(bool bInCombined) const;

private:
    void UpdateSkinningAndRBF(const Eigen::VectorXf& rawControls, const Eigen::VectorXf& joints, std::shared_ptr<BodyGeometry<float>> poseLogic, std::shared_ptr<BodyLogic<float>> poseGeometry) const;

private:
    struct Private;
    Private* m;
};


class BodyShapeEditor::State
{
private:
    State();

public:
    ~State();
    State(const State&);

    const Mesh<float>& GetMesh(int lod) const;
    const std::vector<Eigen::Transform<float, 3, Eigen::Affine>>& GetJointBindMatrices() const;
    const Eigen::VectorX<float>& GetNamedConstraintMeasurements() const;
    const Eigen::VectorX<float>& GetCustomPose() const;
    const Eigen::VectorX<float>& GetPCACoeff() const;

    Eigen::Matrix3Xf GetContourVertices(int ConstraintIndex) const;
    Eigen::Matrix3Xf GetContourDebugVertices(int ConstraintIndex) const;

    void Reset();

    int GetConstraintNum() const;
    const std::string& GetConstraintName(int ConstraintIndex) const;

    bool GetConstraintTarget(int ConstraintIndex, float& OutTarget) const;
    void SetConstraintTarget(int ConstraintIndex, float Target);
    void RemoveConstraintTarget(int ConstraintIndex);

    void SetVertexInfluenceWeights(const SparseMatrix<float>& vertexInfluenceWeights);

    float VertexDeltaScale() const;
    void SetVertexDeltaScale(float VertexDeltaScale);

    void SetSymmetry(const bool sym);
    bool GetSymmetric() const;
    void SetSemanticWeight(float weight);
    float GetSemanticWeight();

    bool GetApplyFloorOffset() const;
    void SetApplyFloorOffset(bool floorOffset);

public:
    State(State&&) = delete;
    State& operator=(State&&) = delete;
    State& operator=(const State&) = delete;

private:
    friend BodyShapeEditor;
    struct Private;
    std::unique_ptr<Private> m;
};

CARBON_NAMESPACE_END(TITAN_NAMESPACE)
