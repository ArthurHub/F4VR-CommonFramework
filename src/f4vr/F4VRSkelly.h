#pragma once

#include "vrcf/VRControllersManager.h"

namespace f4cf::f4vr
{
    /**
     * All the possible values for bones in the skeleton.
     */
    struct SkellyBones
    {
        inline static constexpr std::string_view COM = "COM";
        inline static constexpr std::string_view Pelvis = "Pelvis";

        inline static constexpr std::string_view LLeg_Thigh = "LLeg_Thigh";
        inline static constexpr std::string_view LLeg_Calf = "LLeg_Calf";
        inline static constexpr std::string_view LLeg_Foot = "LLeg_Foot";
        inline static constexpr std::string_view LLeg_Toe1 = "LLeg_Toe1";
        inline static constexpr std::string_view LLeg_Calf_skin = "LLeg_Calf_skin";
        inline static constexpr std::string_view LLeg_Calf_Low_skin = "LLeg_Calf_Low_skin";
        inline static constexpr std::string_view LLeg_Calf_Armor1 = "LLeg_Calf_Armor1";
        inline static constexpr std::string_view LLeg_Calf_Armor2 = "LLeg_Calf_Armor2";
        inline static constexpr std::string_view LLeg_Thigh_skin = "LLeg_Thigh_skin";
        inline static constexpr std::string_view LLeg_Thigh_Low_skin = "LLeg_Thigh_Low_skin";
        inline static constexpr std::string_view LLeg_Thigh_Fat_skin = "LLeg_Thigh_Fat_skin";
        inline static constexpr std::string_view LLeg_Thigh_Armor = "LLeg_Thigh_Armor";

        inline static constexpr std::string_view RLeg_Thigh = "RLeg_Thigh";
        inline static constexpr std::string_view RLeg_Calf = "RLeg_Calf";
        inline static constexpr std::string_view RLeg_Foot = "RLeg_Foot";
        inline static constexpr std::string_view RLeg_Toe1 = "RLeg_Toe1";
        inline static constexpr std::string_view RLeg_Calf_skin = "RLeg_Calf_skin";
        inline static constexpr std::string_view RLeg_Calf_Low_skin = "RLeg_Calf_Low_skin";
        inline static constexpr std::string_view RLeg_Calf_Armor1 = "RLeg_Calf_Armor1";
        inline static constexpr std::string_view RLeg_Calf_Armor2 = "RLeg_Calf_Armor2";
        inline static constexpr std::string_view RLeg_Thigh_skin = "RLeg_Thigh_skin";
        inline static constexpr std::string_view RLeg_Thigh_Low_skin = "RLeg_Thigh_Low_skin";
        inline static constexpr std::string_view RLeg_Thigh_Fat_skin = "RLeg_Thigh_Fat_skin";
        inline static constexpr std::string_view RLeg_Thigh_Armor = "RLeg_Thigh_Armor";

        inline static constexpr std::string_view Pelvis_skin = "Pelvis_skin";
        inline static constexpr std::string_view RButtFat_skin = "RButtFat_skin";
        inline static constexpr std::string_view LButtFat_skin = "LButtFat_skin";
        inline static constexpr std::string_view Pelvis_Rear_skin = "Pelvis_Rear_skin";
        inline static constexpr std::string_view Pelvis_Armor = "Pelvis_Armor";

        inline static constexpr std::string_view SPINE1 = "SPINE1";
        inline static constexpr std::string_view SPINE2 = "SPINE2";
        inline static constexpr std::string_view Chest = "Chest";

        inline static constexpr std::string_view LArm_Collarbone = "LArm_Collarbone";
        inline static constexpr std::string_view LArm_UpperArm = "LArm_UpperArm";
        inline static constexpr std::string_view LArm_ForeArm1 = "LArm_ForeArm1";
        inline static constexpr std::string_view LArm_ForeArm2 = "LArm_ForeArm2";
        inline static constexpr std::string_view LArm_ForeArm3 = "LArm_ForeArm3";
        inline static constexpr std::string_view LArm_Hand = "LArm_Hand";

        inline static constexpr std::string_view LArm_Finger11 = "LArm_Finger11";
        inline static constexpr std::string_view LArm_Finger12 = "LArm_Finger12";
        inline static constexpr std::string_view LArm_Finger13 = "LArm_Finger13";
        inline static constexpr std::string_view LArm_Finger21 = "LArm_Finger21";
        inline static constexpr std::string_view LArm_Finger22 = "LArm_Finger22";
        inline static constexpr std::string_view LArm_Finger23 = "LArm_Finger23";
        inline static constexpr std::string_view LArm_Finger31 = "LArm_Finger31";
        inline static constexpr std::string_view LArm_Finger32 = "LArm_Finger32";
        inline static constexpr std::string_view LArm_Finger33 = "LArm_Finger33";
        inline static constexpr std::string_view LArm_Finger41 = "LArm_Finger41";
        inline static constexpr std::string_view LArm_Finger42 = "LArm_Finger42";
        inline static constexpr std::string_view LArm_Finger43 = "LArm_Finger43";
        inline static constexpr std::string_view LArm_Finger51 = "LArm_Finger51";
        inline static constexpr std::string_view LArm_Finger52 = "LArm_Finger52";
        inline static constexpr std::string_view LArm_Finger53 = "LArm_Finger53";

        inline static constexpr std::string_view WeaponLeft = "WeaponLeft";
        inline static constexpr std::string_view AnimObjectL1 = "AnimObjectL1";
        inline static constexpr std::string_view AnimObjectL2 = "AnimObjectL2";
        inline static constexpr std::string_view AnimObjectL3 = "AnimObjectL3";
        inline static constexpr std::string_view PipboyBone = "PipboyBone";

        inline static constexpr std::string_view LArm_ForeArm3_skin = "LArm_ForeArm3_skin";
        inline static constexpr std::string_view LArm_ForeArm2_skin = "LArm_ForeArm2_skin";
        inline static constexpr std::string_view LArm_ForeArm1_skin = "LArm_ForeArm1_skin";
        inline static constexpr std::string_view LArm_ForeArm_Armor = "LArm_ForeArm_Armor";

        inline static constexpr std::string_view LArm_UpperTwist1 = "LArm_UpperTwist1";
        inline static constexpr std::string_view LArm_UpperTwist2 = "LArm_UpperTwist2";
        inline static constexpr std::string_view LArm_UpperTwist2_skin = "LArm_UpperTwist2_skin";
        inline static constexpr std::string_view LArm_UpperFat_skin = "LArm_UpperFat_skin";
        inline static constexpr std::string_view LArm_UpperTwist1_skin = "LArm_UpperTwist1_skin";
        inline static constexpr std::string_view LArm_UpperArm_skin = "LArm_UpperArm_skin";
        inline static constexpr std::string_view LArm_UpperArm_Armor = "LArm_UpperArm_Armor";
        inline static constexpr std::string_view LArm_Collarbone_skin = "LArm_Collarbone_skin";
        inline static constexpr std::string_view LArm_ShoulderFat_skin = "LArm_ShoulderFat_skin";

        inline static constexpr std::string_view Neck = "Neck";
        inline static constexpr std::string_view Head = "Head";
        inline static constexpr std::string_view Head_skin = "Head_skin";
        inline static constexpr std::string_view Face_skin = "Face_skin";
        inline static constexpr std::string_view Helmet_Armor = "Helmet_Armor";
        inline static constexpr std::string_view Neck_skin = "Neck_skin";
        inline static constexpr std::string_view Neck1_skin = "Neck1_skin";

        inline static constexpr std::string_view RArm_Collarbone = "RArm_Collarbone";
        inline static constexpr std::string_view RArm_UpperArm = "RArm_UpperArm";
        inline static constexpr std::string_view RArm_ForeArm1 = "RArm_ForeArm1";
        inline static constexpr std::string_view RArm_ForeArm2 = "RArm_ForeArm2";
        inline static constexpr std::string_view RArm_ForeArm3 = "RArm_ForeArm3";
        inline static constexpr std::string_view RArm_Hand = "RArm_Hand";

        inline static constexpr std::string_view RArm_Finger11 = "RArm_Finger11";
        inline static constexpr std::string_view RArm_Finger12 = "RArm_Finger12";
        inline static constexpr std::string_view RArm_Finger13 = "RArm_Finger13";
        inline static constexpr std::string_view RArm_Finger21 = "RArm_Finger21";
        inline static constexpr std::string_view RArm_Finger22 = "RArm_Finger22";
        inline static constexpr std::string_view RArm_Finger23 = "RArm_Finger23";
        inline static constexpr std::string_view RArm_Finger31 = "RArm_Finger31";
        inline static constexpr std::string_view RArm_Finger32 = "RArm_Finger32";
        inline static constexpr std::string_view RArm_Finger33 = "RArm_Finger33";
        inline static constexpr std::string_view RArm_Finger41 = "RArm_Finger41";
        inline static constexpr std::string_view RArm_Finger42 = "RArm_Finger42";
        inline static constexpr std::string_view RArm_Finger43 = "RArm_Finger43";
        inline static constexpr std::string_view RArm_Finger51 = "RArm_Finger51";
        inline static constexpr std::string_view RArm_Finger52 = "RArm_Finger52";
        inline static constexpr std::string_view RArm_Finger53 = "RArm_Finger53";

        inline static constexpr std::string_view Weapon = "Weapon";
        inline static constexpr std::string_view AnimObjectR1 = "AnimObjectR1";
        inline static constexpr std::string_view AnimObjectR2 = "AnimObjectR2";
        inline static constexpr std::string_view AnimObjectR3 = "AnimObjectR3";

        inline static constexpr std::string_view RArm_ForeArm3_skin = "RArm_ForeArm3_skin";
        inline static constexpr std::string_view RArm_ForeArm2_skin = "RArm_ForeArm2_skin";
        inline static constexpr std::string_view RArm_ForeArm1_skin = "RArm_ForeArm1_skin";
        inline static constexpr std::string_view RArm_ForeArm_Armor = "RArm_ForeArm_Armor";

        inline static constexpr std::string_view RArm_UpperTwist1 = "RArm_UpperTwist1";
        inline static constexpr std::string_view RArm_UpperTwist2 = "RArm_UpperTwist2";
        inline static constexpr std::string_view RArm_UpperTwist2_skin = "RArm_UpperTwist2_skin";
        inline static constexpr std::string_view RArm_UpperFat_skin = "RArm_UpperFat_skin";
        inline static constexpr std::string_view RArm_UpperTwist1_skin = "RArm_UpperTwist1_skin";
        inline static constexpr std::string_view RArm_UpperArm_skin = "RArm_UpperArm_skin";
        inline static constexpr std::string_view RArm_UpperArm_Armor = "RArm_UpperArm_Armor";
        inline static constexpr std::string_view RArm_Collarbone_skin = "RArm_Collarbone_skin";
        inline static constexpr std::string_view RArm_ShoulderFat_skin = "RArm_ShoulderFat_skin";

        inline static constexpr std::string_view Chest_skin = "Chest_skin";
        inline static constexpr std::string_view LBreast_skin = "LBreast_skin";
        inline static constexpr std::string_view RBreast_skin = "RBreast_skin";
        inline static constexpr std::string_view Chest_Rear_Skin = "Chest_Rear_Skin";
        inline static constexpr std::string_view Chest_Upper_skin = "Chest_Upper_skin";
        inline static constexpr std::string_view Neck_Low_skin = "Neck_Low_skin";

        inline static constexpr std::string_view Back_Armor = "Back_Armor";
        inline static constexpr std::string_view Tank_Armor = "Tank_Armor";
        inline static constexpr std::string_view Wheel = "Wheel";
        inline static constexpr std::string_view ProjectileNode = "ProjectileNode";

        inline static constexpr std::string_view Pauldron_Armor = "Pauldron_Armor";
        inline static constexpr std::string_view L_Pauldron = "L_Pauldron";
        inline static constexpr std::string_view R_Pauldron = "R_Pauldron";

        inline static constexpr std::string_view Spine2_skin = "Spine2_skin";
        inline static constexpr std::string_view UpperBelly_skin = "UpperBelly_skin";
        inline static constexpr std::string_view Spine2_Rear_skin = "Spine2_Rear_skin";
        inline static constexpr std::string_view Spine1_skin = "Spine1_skin";
        inline static constexpr std::string_view Belly_skin = "Belly_skin";
        inline static constexpr std::string_view Spine1_Rear_skin = "Spine1_Rear_skin";

        inline static constexpr std::string_view Camera = "Camera";
        inline static constexpr std::string_view Camera_Control = "Camera Control";
        inline static constexpr std::string_view AnimObjectA = "AnimObjectA";
        inline static constexpr std::string_view AnimObjectB = "AnimObjectB";
        inline static constexpr std::string_view CamTargetParent = "CamTargetParent";
        inline static constexpr std::string_view CamTarget = "CamTarget";
        inline static constexpr std::string_view OpenArmor = "OpenArmor";

        // Extra: not in PA
        inline static constexpr std::string_view L_RibHelper = "L_RibHelper";
        inline static constexpr std::string_view R_RibHelper = "R_RibHelper";
    };

    struct Skelly
    {
        static void initBoneTreeMap();
        static const std::string& getBoneName(int index);
        static RE::NiTransform getBoneWorldTransform(const std::string& boneName);
        static RE::NiPoint3 getIndexFingerTipWorldPosition(const vrcf::Hand& hand);

    private:
        inline static std::map<std::string, int> _boneTreeMap;
        inline static std::vector<std::string> _boneTreeVec;
    };
}
