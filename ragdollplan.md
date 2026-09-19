Yes. I’d build the ragdoll from the model’s **reference pose**, not from whatever animation happens to be playing when the zombie dies. That gives you stable anatomical joint axes and limits.

The linked pack is particularly convenient because it uses **3ds Max Biped + Skin**, so its hierarchy should map cleanly to pelvis/spine/head/upper-arm/forearm/thigh/calf bones. ([Sketchfab][1])

Box3D currently gives us exactly what we need: capsules and spheres, spherical joints with cone/twist limits, revolute joints with angular limits, and negative collision groups specifically recommended for preventing a ragdoll from colliding with itself. ([GitHub][2])

### Ragdoll layout

I'd start with 11 rigid bodies:

```text
                  HEAD
                   ○
                   │ spherical
                 TORSO
             ┌─────┴─────┐
        upper arm     upper arm
            │ hinge          │ hinge
        lower arm     lower arm

                 PELVIS
             ┌─────┴─────┐
         upper leg    upper leg
             │ hinge         │ hinge
         lower leg    lower leg
```

Hands and feet don't initially need their own rigid bodies; they can remain children of the forearm/calf bones.

Here is the core C++ implementation. I'm using GLM for game-side transforms purely for convenience; Box3D itself doesn't require GLM.

```cpp
#include <box3d/box3d.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include <array>
#include <cassert>
#include <cmath>

// -----------------------------------------------------------------------------
// Adapter to your animation system
// -----------------------------------------------------------------------------

struct Transform
{
    glm::vec3 position {0.0f};
    glm::quat rotation {1.0f, 0.0f, 0.0f, 0.0f};
};

// Implement this for whatever glTF/FBX/animation system you're using.
struct SkeletonPose
{
    virtual Transform GetBoneWorldTransform(int bone) const = 0;
    virtual void SetBoneWorldTransform(int bone, const Transform& t) = 0;
};

struct HumanoidBones
{
    int pelvis = -1;
    int spine  = -1;
    int neck   = -1;
    int head   = -1;

    int upperArmL = -1;
    int forearmL  = -1;
    int handL     = -1;

    int upperArmR = -1;
    int forearmR  = -1;
    int handR     = -1;

    int thighL = -1;
    int calfL  = -1;
    int footL  = -1;

    int thighR = -1;
    int calfR  = -1;
    int footR  = -1;
};

// -----------------------------------------------------------------------------
// Math
// -----------------------------------------------------------------------------

static Transform Mul(const Transform& a, const Transform& b)
{
    Transform r;
    r.position = a.position + a.rotation * b.position;
    r.rotation = glm::normalize(a.rotation * b.rotation);
    return r;
}

static Transform Inverse(const Transform& t)
{
    Transform r;
    r.rotation = glm::inverse(t.rotation);
    r.position = r.rotation * -t.position;
    return r;
}

static b3Pos ToB3Pos(const glm::vec3& v)
{
    return {v.x, v.y, v.z};
}

static b3Vec3 ToB3Vec(const glm::vec3& v)
{
    return {v.x, v.y, v.z};
}

static glm::vec3 FromB3(b3Pos v)
{
    return {
        static_cast<float>(v.x),
        static_cast<float>(v.y),
        static_cast<float>(v.z)
    };
}

static b3Quat ToB3(const glm::quat& q)
{
    return {
        {q.x, q.y, q.z},
        q.w
    };
}

static glm::quat FromB3(b3Quat q)
{
    return glm::quat(q.s, q.v.x, q.v.y, q.v.z);
}

static glm::vec3 SafePerpendicular(glm::vec3 v)
{
    v = glm::normalize(v);

    glm::vec3 helper =
        std::abs(v.y) < 0.8f ?
        glm::vec3(0, 1, 0) :
        glm::vec3(1, 0, 0);

    return glm::normalize(glm::cross(v, helper));
}

// Makes an orientation whose local +Y follows "y".
static glm::quat FrameFromY(glm::vec3 y, glm::vec3 xHint)
{
    y = glm::normalize(y);

    glm::vec3 x = xHint - y * glm::dot(xHint, y);

    if (glm::dot(x, x) < 1e-6f)
        x = SafePerpendicular(y);

    x = glm::normalize(x);

    glm::vec3 z = glm::normalize(glm::cross(x, y));
    x = glm::normalize(glm::cross(y, z));

    return glm::normalize(glm::quat_cast(glm::mat3(x, y, z)));
}

// Makes an orientation whose local +Z follows "z".
// Box3D uses joint-frame Z as the hinge/twist axis.
static glm::quat FrameFromZ(glm::vec3 z, glm::vec3 xHint)
{
    z = glm::normalize(z);

    glm::vec3 x = xHint - z * glm::dot(xHint, z);

    if (glm::dot(x, x) < 1e-6f)
        x = SafePerpendicular(z);

    x = glm::normalize(x);

    glm::vec3 y = glm::normalize(glm::cross(z, x));
    x = glm::normalize(glm::cross(y, z));

    return glm::normalize(glm::quat_cast(glm::mat3(x, y, z)));
}

static float CapsuleVolume(float length, float radius)
{
    constexpr float PI = 3.14159265358979323846f;

    // Cylinder + two hemispheres = cylinder + sphere
    return PI * radius * radius * length
         + (4.0f / 3.0f) * PI * radius * radius * radius;
}

static float SphereVolume(float radius)
{
    constexpr float PI = 3.14159265358979323846f;
    return (4.0f / 3.0f) * PI * radius * radius * radius;
}

// -----------------------------------------------------------------------------
// Ragdoll
// -----------------------------------------------------------------------------

class ZombieRagdoll
{
public:
    enum Part
    {
        Pelvis,

        Torso,
        Head,

        UpperArmL,
        LowerArmL,

        UpperArmR,
        LowerArmR,

        UpperLegL,
        LowerLegL,

        UpperLegR,
        LowerLegR,

        PartCount
    };

    struct Body
    {
        b3BodyId id = b3_nullBodyId;

        int bone = -1;

        // Rest relation:
        //
        // boneWorld = bodyWorld * bodyToBone
        //
        Transform bodyToBone;
    };

    void Build(
        b3WorldId world,
        const SkeletonPose& referencePose,
        const HumanoidBones& bones,
        glm::vec3 characterForward,
        int ragdollIndex)
    {
        m_world = world;

        characterForward = glm::normalize(characterForward);

        // Give each ragdoll a unique negative collision group.
        //
        // Shapes with the same negative group do not collide.
        m_collisionGroup = -(ragdollIndex + 1);

        const Transform pelvis = referencePose.GetBoneWorldTransform(bones.pelvis);
        const Transform spine  = referencePose.GetBoneWorldTransform(bones.spine);
        const Transform neck   = referencePose.GetBoneWorldTransform(bones.neck);
        const Transform head   = referencePose.GetBoneWorldTransform(bones.head);

        const Transform ula = referencePose.GetBoneWorldTransform(bones.upperArmL);
        const Transform fla = referencePose.GetBoneWorldTransform(bones.forearmL);
        const Transform hla = referencePose.GetBoneWorldTransform(bones.handL);

        const Transform ura = referencePose.GetBoneWorldTransform(bones.upperArmR);
        const Transform fra = referencePose.GetBoneWorldTransform(bones.forearmR);
        const Transform hra = referencePose.GetBoneWorldTransform(bones.handR);

        const Transform tl = referencePose.GetBoneWorldTransform(bones.thighL);
        const Transform cl = referencePose.GetBoneWorldTransform(bones.calfL);
        const Transform fl = referencePose.GetBoneWorldTransform(bones.footL);

        const Transform tr = referencePose.GetBoneWorldTransform(bones.thighR);
        const Transform cr = referencePose.GetBoneWorldTransform(bones.calfR);
        const Transform fr = referencePose.GetBoneWorldTransform(bones.footR);

        float shoulderWidth =
            glm::length(ura.position - ula.position);

        float hipWidth =
            glm::length(tr.position - tl.position);

        // ---------------------------------------------------------------------
        // Pelvis
        // ---------------------------------------------------------------------

        float pelvisRadius = hipWidth * 0.40f;

        AddCapsule(
            Pelvis,
            bones.pelvis,
            tl.position,
            tr.position,
            pelvisRadius,
            12.0f,
            pelvis);

        // ---------------------------------------------------------------------
        // Torso
        // ---------------------------------------------------------------------

        float torsoRadius = shoulderWidth * 0.34f;

        AddCapsule(
            Torso,
            bones.spine,
            spine.position,
            neck.position,
            torsoRadius,
            25.0f,
            spine);

        // ---------------------------------------------------------------------
        // Head
        // ---------------------------------------------------------------------

        float neckHeadLength =
            glm::length(head.position - neck.position);

        float headRadius = neckHeadLength * 0.55f;

        AddSphere(
            Head,
            bones.head,
            head.position,
            headRadius,
            5.0f,
            head);

        // ---------------------------------------------------------------------
        // Arms
        // ---------------------------------------------------------------------

        float upperArmLengthL = glm::length(fla.position - ula.position);
        float lowerArmLengthL = glm::length(hla.position - fla.position);

        float upperArmLengthR = glm::length(fra.position - ura.position);
        float lowerArmLengthR = glm::length(hra.position - fra.position);

        AddCapsule(
            UpperArmL,
            bones.upperArmL,
            ula.position,
            fla.position,
            upperArmLengthL * 0.18f,
            2.2f,
            ula);

        AddCapsule(
            LowerArmL,
            bones.forearmL,
            fla.position,
            hla.position,
            lowerArmLengthL * 0.16f,
            1.6f,
            fla);

        AddCapsule(
            UpperArmR,
            bones.upperArmR,
            ura.position,
            fra.position,
            upperArmLengthR * 0.18f,
            2.2f,
            ura);

        AddCapsule(
            LowerArmR,
            bones.forearmR,
            fra.position,
            hra.position,
            lowerArmLengthR * 0.16f,
            1.6f,
            fra);

        // ---------------------------------------------------------------------
        // Legs
        // ---------------------------------------------------------------------

        float upperLegLengthL = glm::length(cl.position - tl.position);
        float lowerLegLengthL = glm::length(fl.position - cl.position);

        float upperLegLengthR = glm::length(cr.position - tr.position);
        float lowerLegLengthR = glm::length(fr.position - cr.position);

        AddCapsule(
            UpperLegL,
            bones.thighL,
            tl.position,
            cl.position,
            upperLegLengthL * 0.19f,
            7.5f,
            tl);

        AddCapsule(
            LowerLegL,
            bones.calfL,
            cl.position,
            fl.position,
            lowerLegLengthL * 0.16f,
            4.0f,
            cl);

        AddCapsule(
            UpperLegR,
            bones.thighR,
            tr.position,
            cr.position,
            upperLegLengthR * 0.19f,
            7.5f,
            tr);

        AddCapsule(
            LowerLegR,
            bones.calfR,
            cr.position,
            fr.position,
            lowerLegLengthR * 0.16f,
            4.0f,
            cr);

        // ---------------------------------------------------------------------
        // Joints
        // ---------------------------------------------------------------------

        // Pelvis -> spine
        AddBallJoint(
            Pelvis,
            Torso,
            spine.position,
            neck.position - spine.position,
            characterForward,
            25.0f,     // swing
            -20.0f,    // twist
            20.0f);

        // Torso -> head
        AddBallJoint(
            Torso,
            Head,
            neck.position,
            head.position - neck.position,
            characterForward,
            35.0f,
            -40.0f,
            40.0f);

        // Shoulders
        AddBallJoint(
            Torso,
            UpperArmL,
            ula.position,
            fla.position - ula.position,
            characterForward,
            85.0f,
            -70.0f,
            70.0f);

        AddBallJoint(
            Torso,
            UpperArmR,
            ura.position,
            fra.position - ura.position,
            characterForward,
            85.0f,
            -70.0f,
            70.0f);

        // Elbows.
        //
        // We construct a hinge so positive motion bends roughly forward.
        AddHingeJoint(
            UpperArmL,
            LowerArmL,
            fla.position,
            fla.position - ula.position,
            characterForward,
            -5.0f,
            145.0f);

        AddHingeJoint(
            UpperArmR,
            LowerArmR,
            fra.position,
            fra.position - ura.position,
            characterForward,
            -5.0f,
            145.0f);

        // Hips
        AddBallJoint(
            Pelvis,
            UpperLegL,
            tl.position,
            cl.position - tl.position,
            characterForward,
            55.0f,
            -30.0f,
            30.0f);

        AddBallJoint(
            Pelvis,
            UpperLegR,
            tr.position,
            cr.position - tr.position,
            characterForward,
            55.0f,
            -30.0f,
            30.0f);

        // Knees bend backwards relative to character forward.
        AddHingeJoint(
            UpperLegL,
            LowerLegL,
            cl.position,
            cl.position - tl.position,
            -characterForward,
            -5.0f,
            150.0f);

        AddHingeJoint(
            UpperLegR,
            LowerLegR,
            cr.position,
            cr.position - tr.position,
            -characterForward,
            -5.0f,
            150.0f);
    }

    // -------------------------------------------------------------------------
    // Switch from animation to physics
    // -------------------------------------------------------------------------

    void Activate(
        const SkeletonPose& animatedPose,
        glm::vec3 characterVelocity)
    {
        if (m_active)
            return;

        // First place every physics body on top of the current animated pose.
        for (Body& body : m_bodies)
        {
            Transform boneWorld =
                animatedPose.GetBoneWorldTransform(body.bone);

            // boneWorld = bodyWorld * bodyToBone
            //
            // therefore:
            //
            // bodyWorld = boneWorld * inverse(bodyToBone)
            //
            Transform bodyWorld =
                Mul(boneWorld, Inverse(body.bodyToBone));

            b3Body_SetTransform(
                body.id,
                ToB3Pos(bodyWorld.position),
                ToB3(bodyWorld.rotation));

            b3Body_SetLinearVelocity(
                body.id,
                ToB3Vec(characterVelocity));

            b3Body_SetAngularVelocity(
                body.id,
                {0.0f, 0.0f, 0.0f});
        }

        // Enabling after all bodies have been positioned prevents a single
        // partially-created ragdoll from interacting with the world.
        for (Body& body : m_bodies)
        {
            b3Body_Enable(body.id);
            b3Body_SetAwake(body.id, true);
        }

        m_active = true;
    }

    // -------------------------------------------------------------------------
    // Physics -> animation skeleton
    // -------------------------------------------------------------------------

    void WriteBackToSkeleton(SkeletonPose& pose)
    {
        if (!m_active)
            return;

        // These were deliberately created roughly parent-to-child.
        // Your skeleton implementation should either:
        //
        // 1. support world-space bone writes, or
        // 2. convert these to parent-local transforms afterwards.

        static constexpr Part updateOrder[] =
        {
            Pelvis,
            Torso,
            Head,

            UpperArmL,
            LowerArmL,

            UpperArmR,
            LowerArmR,

            UpperLegL,
            LowerLegL,

            UpperLegR,
            LowerLegR
        };

        for (Part part : updateOrder)
        {
            Body& body = m_bodies[part];

            Transform bodyWorld;
            bodyWorld.position =
                FromB3(b3Body_GetPosition(body.id));

            bodyWorld.rotation =
                FromB3(b3Body_GetRotation(body.id));

            Transform boneWorld =
                Mul(bodyWorld, body.bodyToBone);

            pose.SetBoneWorldTransform(
                body.bone,
                boneWorld);
        }
    }

    bool IsActive() const
    {
        return m_active;
    }

private:
    // -------------------------------------------------------------------------
    // Body creation
    // -------------------------------------------------------------------------

    void AddCapsule(
        Part part,
        int bone,
        glm::vec3 p0,
        glm::vec3 p1,
        float radius,
        float desiredMassKg,
        const Transform& boneRest)
    {
        glm::vec3 axis = p1 - p0;
        float length = glm::length(axis);

        assert(length > 0.001f);

        glm::vec3 center = (p0 + p1) * 0.5f;

        glm::quat bodyRotation =
            FrameFromY(axis, glm::vec3(0, 0, 1));

        Transform bodyRest {
            center,
            bodyRotation
        };

        b3BodyDef bodyDef = b3DefaultBodyDef();

        bodyDef.type = b3_dynamicBody;
        bodyDef.position = ToB3Pos(center);
        bodyDef.rotation = ToB3(bodyRotation);

        // Don't simulate until Activate().
        bodyDef.isEnabled = false;

        // Mild damping works well for floppy characters.
        bodyDef.linearDamping = 0.05f;
        bodyDef.angularDamping = 0.10f;

        b3BodyId id =
            b3CreateBody(m_world, &bodyDef);

        // Capsule axis is local Y.
        b3Capsule capsule {
            {0.0f, -length * 0.5f, 0.0f},
            {0.0f,  length * 0.5f, 0.0f},
            radius
        };

        float volume = CapsuleVolume(length, radius);
        float density = desiredMassKg / volume;

        b3ShapeDef shapeDef = b3DefaultShapeDef();

        shapeDef.density = density;
        shapeDef.baseMaterial.friction = 0.6f;
        shapeDef.baseMaterial.restitution = 0.0f;

        shapeDef.filter.groupIndex =
            m_collisionGroup;

        b3CreateCapsuleShape(
            id,
            &shapeDef,
            &capsule);

        Body& body = m_bodies[part];

        body.id = id;
        body.bone = bone;

        // Rest relation between rigid body and animation bone.
        body.bodyToBone =
            Mul(Inverse(bodyRest), boneRest);
    }

    void AddSphere(
        Part part,
        int bone,
        glm::vec3 center,
        float radius,
        float desiredMassKg,
        const Transform& boneRest)
    {
        Transform bodyRest {
            center,
            boneRest.rotation
        };

        b3BodyDef bodyDef = b3DefaultBodyDef();

        bodyDef.type = b3_dynamicBody;
        bodyDef.position = ToB3Pos(center);
        bodyDef.rotation = ToB3(bodyRest.rotation);
        bodyDef.isEnabled = false;

        bodyDef.linearDamping = 0.05f;
        bodyDef.angularDamping = 0.10f;

        b3BodyId id =
            b3CreateBody(m_world, &bodyDef);

        b3Sphere sphere {
            {0.0f, 0.0f, 0.0f},
            radius
        };

        float density =
            desiredMassKg / SphereVolume(radius);

        b3ShapeDef shapeDef = b3DefaultShapeDef();

        shapeDef.density = density;
        shapeDef.baseMaterial.friction = 0.6f;
        shapeDef.baseMaterial.restitution = 0.0f;

        shapeDef.filter.groupIndex =
            m_collisionGroup;

        b3CreateSphereShape(
            id,
            &shapeDef,
            &sphere);

        Body& body = m_bodies[part];

        body.id = id;
        body.bone = bone;

        body.bodyToBone =
            Mul(Inverse(bodyRest), boneRest);
    }

    // -------------------------------------------------------------------------
    // Joint helpers
    // -------------------------------------------------------------------------

    b3Transform LocalJointFrame(
        b3BodyId body,
        glm::vec3 worldPivot,
        glm::quat worldFrame)
    {
        b3Transform t = b3Transform_identity;

        t.p =
            b3Body_GetLocalPoint(
                body,
                ToB3Pos(worldPivot));

        b3Quat bodyRotation =
            b3Body_GetRotation(body);

        // inv(bodyRotation) * worldFrame
        t.q =
            b3InvMulQuat(
                bodyRotation,
                ToB3(worldFrame));

        return t;
    }

    void AddBallJoint(
        Part parent,
        Part child,
        glm::vec3 pivot,
        glm::vec3 childAxis,
        glm::vec3 orientationHint,
        float coneDegrees,
        float lowerTwistDegrees,
        float upperTwistDegrees)
    {
        b3BodyId a = m_bodies[parent].id;
        b3BodyId b = m_bodies[child].id;

        glm::quat jointWorldFrame =
            FrameFromZ(
                childAxis,
                orientationHint);

        b3SphericalJointDef joint =
            b3DefaultSphericalJointDef();

        joint.base.bodyIdA = a;
        joint.base.bodyIdB = b;

        joint.base.localFrameA =
            LocalJointFrame(
                a,
                pivot,
                jointWorldFrame);

        joint.base.localFrameB =
            LocalJointFrame(
                b,
                pivot,
                jointWorldFrame);

        joint.base.collideConnected = false;

        joint.enableConeLimit = true;
        joint.coneAngle =
            coneDegrees * B3_DEG_TO_RAD;

        joint.enableTwistLimit = true;

        joint.lowerTwistAngle =
            lowerTwistDegrees * B3_DEG_TO_RAD;

        joint.upperTwistAngle =
            upperTwistDegrees * B3_DEG_TO_RAD;

        b3CreateSphericalJoint(
            m_world,
            &joint);
    }

    void AddHingeJoint(
        Part parent,
        Part child,
        glm::vec3 pivot,
        glm::vec3 limbDirection,
        glm::vec3 desiredBendDirection,
        float lowerDegrees,
        float upperDegrees)
    {
        glm::vec3 dir =
            glm::normalize(limbDirection);

        // Project desired bend direction perpendicular to limb.
        glm::vec3 bend =
            desiredBendDirection -
            dir * glm::dot(
                desiredBendDirection,
                dir);

        if (glm::dot(bend, bend) < 1e-6f)
            bend = SafePerpendicular(dir);

        bend = glm::normalize(bend);

        // Important property:
        //
        // axis = dir x bend
        //
        // A positive rotation around this axis moves "dir"
        // toward "bend".
        glm::vec3 hingeAxis =
            glm::normalize(glm::cross(dir, bend));

        glm::quat jointWorldFrame =
            FrameFromZ(
                hingeAxis,
                dir);

        b3BodyId a = m_bodies[parent].id;
        b3BodyId b = m_bodies[child].id;

        b3RevoluteJointDef joint =
            b3DefaultRevoluteJointDef();

        joint.base.bodyIdA = a;
        joint.base.bodyIdB = b;

        joint.base.localFrameA =
            LocalJointFrame(
                a,
                pivot,
                jointWorldFrame);

        joint.base.localFrameB =
            LocalJointFrame(
                b,
                pivot,
                jointWorldFrame);

        joint.base.collideConnected = false;

        joint.enableLimit = true;

        joint.lowerAngle =
            lowerDegrees * B3_DEG_TO_RAD;

        joint.upperAngle =
            upperDegrees * B3_DEG_TO_RAD;

        b3CreateRevoluteJoint(
            m_world,
            &joint);
    }

private:
    b3WorldId m_world = b3_nullWorldId;

    std::array<Body, PartCount> m_bodies {};

    int m_collisionGroup = -1;

    bool m_active = false;
};
```

The API usage above follows Box3D's current joint model: the Z axis of the local joint frame is the revolute hinge axis, while spherical joints use their frame Z axes for cone/twist restriction. ([GitHub][2])

### Mapping the zombie skeleton

Because the asset is a 3ds Max Biped rig, I would initially look for names along these lines:

```cpp
HumanoidBones bones;

// Actual prefixes can differ:
// "Bip001", "Bip01", etc.

bones.pelvis = FindBoneEndingWith("Pelvis");

bones.spine = FindBoneEndingWith("Spine");
bones.neck  = FindBoneEndingWith("Neck");
bones.head  = FindBoneEndingWith("Head");

bones.upperArmL = FindBoneEndingWith("L UpperArm");
bones.forearmL  = FindBoneEndingWith("L Forearm");
bones.handL     = FindBoneEndingWith("L Hand");

bones.upperArmR = FindBoneEndingWith("R UpperArm");
bones.forearmR  = FindBoneEndingWith("R Forearm");
bones.handR     = FindBoneEndingWith("R Hand");

bones.thighL = FindBoneEndingWith("L Thigh");
bones.calfL  = FindBoneEndingWith("L Calf");
bones.footL  = FindBoneEndingWith("L Foot");

bones.thighR = FindBoneEndingWith("R Thigh");
bones.calfR  = FindBoneEndingWith("R Calf");
bones.footR  = FindBoneEndingWith("R Foot");
```

I'd make this suffix-based rather than hardcoding `Bip001`, because Max changes that prefix easily.

Then construction is essentially:

```cpp
ZombieRagdoll ragdoll;

ragdoll.Build(
    physicsWorld,
    zombieReferencePose,
    bones,

    // Must match your model convention.
    glm::vec3(0.0f, 0.0f, 1.0f),

    zombieEntityId
);
```

When the zombie dies:

```cpp
ragdoll.Activate(
    zombieAnimatedPose,
    zombieVelocity
);
```

Your physics update becomes:

```cpp
constexpr float physicsDt = 1.0f / 60.0f;

b3World_Step(
    physicsWorld,
    physicsDt,
    4
);

ragdoll.WriteBackToSkeleton(
    zombiePose
);
```

Box3D's own documentation recommends working in approximately **meters, kilograms and seconds**, so make sure your imported skeleton positions are in meters before passing them into this code. ([GitHub][3])

### Why I built it this way

The particularly important bit is this relationship:

```text
animation:

bone transform
     │
     ▼
physics body


death occurs
     │
     ▼
Box3D takes control


physics body
     │
     ▼
bone transform
     │
     ▼
skinned zombie mesh
```

The stored `bodyToBone` transform means the physics capsule doesn't have to line up exactly with the bone's origin or rotation.

That's important because something like an upper-leg bone might have its origin at the hip:

```text
hip bone origin
      ↓
      ●
      │
      │   physics capsule
     ╭│╮
     │││
     │││
     ╰│╯
      │
      ● knee
```

The body itself is centered halfway down the thigh, while the animation bone is at the hip. `bodyToBone` preserves that offset.

I also intentionally used **negative collision groups** rather than merely disabling collisions between connected limbs. Box3D specifically documents negative group indices as the way to make every shape belonging to one ragdoll ignore every other shape in the same ragdoll while still allowing different ragdolls to collide. ([GitHub][4])

The ~73 kg mass distribution in the sample is also deliberate. Rather than choosing arbitrary densities, each collider gets a requested body-part mass and the code computes:

```text
density = desired mass / collider volume
```

so Box3D can still automatically calculate sensible inertia tensors.

The first thing I'd add after this is a **physics debug view that draws all 11 capsules/spheres and their joint axes over the animated zombie**. That will make tuning this particular model dramatically easier, because you'll immediately see whether the Biped bone positions, forward axis, capsule radii, and knee/elbow bend directions match the exported skeleton.

[1]: https://sketchfab.com/3d-models/polyart-zombies-with-animations-free-pack-d9bcfdd88f5348549bc947226af7c314?utm_source=chatgpt.com "Polyart Zombies with Animations Free Pack - Download Free 3D model by Denys Almaral (@denysalmaral) [d9bcfdd] - Sketchfab"
[2]: https://github.com/erincatto/box3d/blob/main/docs/simulation.md?utm_source=chatgpt.com "box3d/docs/simulation.md at main · erincatto/box3d · GitHub"
[3]: https://github.com/erincatto/box3d/blob/main/docs/hello.md?utm_source=chatgpt.com "box3d/docs/hello.md at main · erincatto/box3d · GitHub"
[4]: https://github.com/erincatto/box3d/blob/main/include/box3d/types.h?utm_source=chatgpt.com "box3d/include/box3d/types.h at main · erincatto/box3d · GitHub"
