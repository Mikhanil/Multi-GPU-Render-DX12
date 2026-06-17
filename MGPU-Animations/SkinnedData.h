#ifndef SKINNEDDATA_H
#define SKINNEDDATA_H

#include "Common/d3dUtil.h"
#include "Common/MathHelper.h"
#include <unordered_map>
///<summary>
/// A Keyframe defines the bone transformation at an instant in time.
///</summary>
struct Keyframe
{
    Keyframe();
    ~Keyframe();

    float TimePos;
    DirectX::XMFLOAT3 Translation;
    DirectX::XMFLOAT3 Scale;
    DirectX::XMFLOAT4 RotationQuat;
};

///<summary>
/// A BoneAnimation is defined by a list of keyframes.  For time
/// values inbetween two keyframes, we interpolate between the
/// two nearest keyframes that bound the time.  
///
/// We assume an animation always has two keyframes.
///</summary>
struct BoneAnimation
{
    float GetStartTime() const;
    float GetEndTime() const;

    void Interpolate(float t, DirectX::XMFLOAT4X4& M, DirectX::XMFLOAT3& angular, DirectX::XMFLOAT3& linear) const;

    std::vector<Keyframe> Keyframes;
};

///<summary>
/// Examples of AnimationClips are "Walk", "Run", "Attack", "Defend".
/// An AnimationClip requires a BoneAnimation for every bone to form
/// the animation clip.    
///</summary>
struct AnimationClip
{
    float GetClipStartTime() const;
    float GetClipEndTime() const;

    void Interpolate(float t,
                     std::vector<DirectX::XMFLOAT4X4>& boneTransforms,
                     std::vector<DirectX::XMFLOAT3>& angular,
                     std::vector<DirectX::XMFLOAT3>& linear) const;

    std::vector<BoneAnimation> BoneAnimations;
};

class SkinnedData
{
public:
    UINT BoneCount() const;

    float GetClipStartTime(const std::string& clipName) const;
    float GetClipEndTime(const std::string& clipName) const;

    void Set(
        std::vector<int>& boneHierarchy,
        std::vector<DirectX::XMFLOAT4X4>& boneOffsets,
        std::unordered_map<std::string, AnimationClip>& animations);

    // In a real project, you'd want to cache the result if there was a chance
    // that you were calling this several times with the same clipName at 
    // the same timePos.
    void GetFinalTransforms(const std::string& clipName, float timePos,
                            std::vector<DirectX::XMFLOAT4X4>& finalTransforms,
                            std::vector<DirectX::XMFLOAT3>& angularVelocities,
                            std::vector<DirectX::XMFLOAT3>& linearVelocities) const;


    [[nodiscard]] std::vector<int> getBoneHierarchy() const
    {
        return mBoneHierarchy;
    }
    
    [[nodiscard]] std::vector<DirectX::XMFLOAT4X4> getBoneOffsets() const
    {
        return mBoneOffsets;
    }

private:
    // Gives parentIndex of ith bone.
    std::vector<int> mBoneHierarchy;


    std::vector<DirectX::XMFLOAT4X4> mBoneOffsets;



    std::unordered_map<std::string, AnimationClip> mAnimations;
};

DirectX::XMVECTOR ConjugateQuat(DirectX::XMVECTOR& q);
void QuatToAxisAngle(DirectX::XMVECTOR const& q, DirectX::XMVECTOR& axis, float& angle);
#endif // SKINNEDDATA_H
