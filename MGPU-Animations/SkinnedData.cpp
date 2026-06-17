#include "SkinnedData.h"

using namespace DirectX;

Keyframe::Keyframe()
    : TimePos(0.0f),
      Translation(0.0f, 0.0f, 0.0f),
      Scale(1.0f, 1.0f, 1.0f),
      RotationQuat(0.0f, 0.0f, 0.0f, 1.0f)
{
}

Keyframe::~Keyframe()
{
}

float BoneAnimation::GetStartTime() const
{
    // Keyframes are sorted by time, so first keyframe gives start time.
    return Keyframes.front().TimePos;
}

float BoneAnimation::GetEndTime() const
{
    // Keyframes are sorted by time, so last keyframe gives end time.
    float f = Keyframes.back().TimePos;

    return f;
}

void BoneAnimation::Interpolate(float t, XMFLOAT4X4& M, XMFLOAT3& angular, XMFLOAT3& linear) const
{
    if (t <= Keyframes.front().TimePos)
    {
        XMVECTOR S = XMLoadFloat3(&Keyframes.front().Scale);
        XMVECTOR P = XMLoadFloat3(&Keyframes.front().Translation);
        XMVECTOR Q = XMLoadFloat4(&Keyframes.front().RotationQuat);

        XMVECTOR zero = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
        XMStoreFloat4x4(&M, XMMatrixAffineTransformation(S, zero, Q, P));


        
        //velocities
        auto size = Keyframes.size();
        XMVECTOR SV = XMLoadFloat3(&Keyframes[size-2].Scale);
        XMVECTOR PV = XMLoadFloat3(&Keyframes[size-2].Translation);
        XMVECTOR QV = XMLoadFloat4(&Keyframes[size-2].RotationQuat);
        
        //angular
        XMVECTOR q = Q * ConjugateQuat(QV);
        XMVECTOR axis;
        float angle;
        QuatToAxisAngle(q, axis, angle);
        XMVECTOR AngularVelocity = axis * angle / (t - Keyframes[size-2].TimePos) ;
        XMStoreFloat3(&angular, AngularVelocity);
        //linear
        XMVECTOR LinearVelocity = (P - PV)/(t - Keyframes[size-2].TimePos); 
        XMStoreFloat3(&linear, LinearVelocity);
    }
    else if (t >= Keyframes.back().TimePos)
    {
        XMVECTOR S = XMLoadFloat3(&Keyframes.back().Scale);
        XMVECTOR P = XMLoadFloat3(&Keyframes.back().Translation);
        XMVECTOR Q = XMLoadFloat4(&Keyframes.back().RotationQuat);

        XMVECTOR zero = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
        XMStoreFloat4x4(&M, XMMatrixAffineTransformation(S, zero, Q, P));


        //velocities
        auto size = Keyframes.size();
        XMVECTOR SV = XMLoadFloat3(&Keyframes[size-2].Scale);
        XMVECTOR PV = XMLoadFloat3(&Keyframes[size-2].Translation);
        XMVECTOR QV = XMLoadFloat4(&Keyframes[size-2].RotationQuat);
        
        //angular
        XMVECTOR q = Q * ConjugateQuat(QV);
        XMVECTOR axis;
        float angle;
        QuatToAxisAngle(q, axis, angle);
        XMVECTOR AngularVelocity = axis * angle / (t - Keyframes[size-2].TimePos) ;
        XMStoreFloat3(&angular, AngularVelocity);
        //linear
        XMVECTOR LinearVelocity = (P - PV)/(t - Keyframes[size-2].TimePos); 
        XMStoreFloat3(&linear, LinearVelocity);
    }
    else
    {
        for (UINT i = 0; i < Keyframes.size() - 1; ++i)
        {
            if (t >= Keyframes[i].TimePos && t <= Keyframes[i + 1].TimePos)
            {
                float lerpPercent = (t - Keyframes[i].TimePos) / (Keyframes[i + 1].TimePos - Keyframes[i].TimePos);

                XMVECTOR s0 = XMLoadFloat3(&Keyframes[i].Scale);
                XMVECTOR s1 = XMLoadFloat3(&Keyframes[i + 1].Scale);

                XMVECTOR p0 = XMLoadFloat3(&Keyframes[i].Translation);
                XMVECTOR p1 = XMLoadFloat3(&Keyframes[i + 1].Translation);

                XMVECTOR q0 = XMLoadFloat4(&Keyframes[i].RotationQuat);
                XMVECTOR q1 = XMLoadFloat4(&Keyframes[i + 1].RotationQuat);

                XMVECTOR S = XMVectorLerp(s0, s1, lerpPercent);
                XMVECTOR P = XMVectorLerp(p0, p1, lerpPercent);
                XMVECTOR Q = XMQuaternionSlerp(q0, q1, lerpPercent);

                XMVECTOR zero = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
                XMStoreFloat4x4(&M, XMMatrixAffineTransformation(S, zero, Q, P));


                //velocities
                XMVECTOR SV = XMLoadFloat3(&Keyframes[i].Scale);
                XMVECTOR PV = XMLoadFloat3(&Keyframes[i].Translation);
                XMVECTOR QV = XMLoadFloat4(&Keyframes[i].RotationQuat);
        
                //angular
                XMVECTOR q = Q * ConjugateQuat(QV);
                XMVECTOR axis;
                float angle;
                QuatToAxisAngle(q, axis, angle);
                
                XMVECTOR AngularVelocity = axis * angle / (t - Keyframes[i].TimePos) ;
                XMStoreFloat3(&angular, AngularVelocity);
                //linear
                XMVECTOR LinearVelocity = (P - PV)/(t - Keyframes[i].TimePos); 
                XMStoreFloat3(&linear, LinearVelocity);
                break;
            }
        }
    }
}

float AnimationClip::GetClipStartTime() const
{
    // Find smallest start time over all bones in this clip.
    float t = MathHelper::Infinity;
    for (UINT i = 0; i < BoneAnimations.size(); ++i)
    {
        t = MathHelper::Min(t, BoneAnimations[i].GetStartTime());
    }

    return t;
}

float AnimationClip::GetClipEndTime() const
{
    // Find largest end time over all bones in this clip.
    float t = 0.0f;
    for (UINT i = 0; i < BoneAnimations.size(); ++i)
    {
        t = MathHelper::Max(t, BoneAnimations[i].GetEndTime());
    }

    return t;
}

void AnimationClip::Interpolate(float t, std::vector<XMFLOAT4X4>& boneTransforms,
                                std::vector<XMFLOAT3>& angular, std::vector<XMFLOAT3>& linear) const
{
    for (UINT i = 0; i < BoneAnimations.size(); ++i)
    {
        BoneAnimations[i].Interpolate(t, boneTransforms[i], angular[i], linear[i]);
    }
}

float SkinnedData::GetClipStartTime(const std::string& clipName) const
{
    auto clip = mAnimations.find(clipName);
    return clip->second.GetClipStartTime();
}

float SkinnedData::GetClipEndTime(const std::string& clipName) const
{
    auto clip = mAnimations.find(clipName);
    return clip->second.GetClipEndTime();
}

UINT SkinnedData::BoneCount() const
{
    return mBoneHierarchy.size();
}

void SkinnedData::Set(std::vector<int>& boneHierarchy,
                      std::vector<XMFLOAT4X4>& boneOffsets,
                      std::unordered_map<std::string, AnimationClip>& animations)
{
    mBoneHierarchy = boneHierarchy;
    mBoneOffsets = boneOffsets;
    mAnimations = animations;
}

void SkinnedData::GetFinalTransforms(const std::string& clipName,
                                     float timePos,
                                     std::vector<XMFLOAT4X4>& finalTransforms,
                                     std::vector<XMFLOAT3>& angularVelocities,
                                     std::vector<XMFLOAT3>& linearVelocities) const
{
    UINT numBones = mBoneOffsets.size();

    std::vector<XMFLOAT4X4> toParentTransforms(numBones);
    std::vector<XMFLOAT3> angular(numBones);
    std::vector<XMFLOAT3> linear(numBones);
    // Interpolate all the bones of this clip at the given time instance.
    auto clip = mAnimations.find(clipName);
    clip->second.Interpolate(timePos, toParentTransforms, angular, linear);

    //
    // Traverse the hierarchy and transform all the bones to the root space.
    //

    std::vector<XMFLOAT4X4> toRootTransforms(numBones);

    // The root bone has index 0.  The root bone has no parent, so its toRootTransform
    // is just its local bone transform.
    toRootTransforms[0] = toParentTransforms[0];

    // Now find the toRootTransform of the children.
    for (UINT i = 1; i < numBones; ++i)
    {
        XMMATRIX toParent = XMLoadFloat4x4(&toParentTransforms[i]);

        int parentIndex = mBoneHierarchy[i];
        XMMATRIX parentToRoot = XMLoadFloat4x4(&toRootTransforms[parentIndex]);

        XMMATRIX toRoot = XMMatrixMultiply(toParent, parentToRoot);

        XMStoreFloat4x4(&toRootTransforms[i], toRoot);
    }

    // Premultiply by the bone offset transform to get the final transform.
    for (UINT i = 0; i < numBones; ++i)
    {
        XMMATRIX offset = XMLoadFloat4x4(&mBoneOffsets[i]);
        XMMATRIX toRoot = XMLoadFloat4x4(&toRootTransforms[i]);
        XMMATRIX finalTransform = XMMatrixMultiply(offset, toRoot);
        XMStoreFloat4x4(&finalTransforms[i], XMMatrixTranspose(finalTransform));
        angularVelocities[i] = angular[i];
        linearVelocities[i] = linear[i];
    }
}


XMVECTOR ConjugateQuat(XMVECTOR& q) 
{
    return XMVECTOR{-XMVectorGetX(q), -XMVectorGetY(q), -XMVectorGetZ(q), XMVectorGetW(q)};
}

void QuatToAxisAngle(XMVECTOR const& q, XMVECTOR& axis, float& angle)
{
    XMVECTOR n_sin = {XMVectorGetX(q), XMVectorGetY(q), XMVectorGetZ(q)};
    float s = std::sqrt(XMVectorGetX(XMVector3Dot(n_sin, n_sin)));

    if (s < 1e-6)
    {
        axis = {1,0,0};
        angle = .0f;
        return;
    }
    
    angle = 2 *std::atan2(s, XMVectorGetW(q));
    axis = n_sin/s;
}