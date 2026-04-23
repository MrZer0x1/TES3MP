#ifndef OPENMW_MECHANICS_OBSTACLE_H
#define OPENMW_MECHANICS_OBSTACLE_H

#include <osg/Vec3f>

namespace MWWorld
{
    class Ptr;
}

namespace MWMechanics
{
    struct Movement;

    static constexpr int NUM_EVADE_DIRECTIONS = 4;

    /// tests actor's proximity to a closed door by default
    bool proximityToDoor(const MWWorld::Ptr& actor, float minDist);

    /// Returns door pointer within range. No guarantee is given as to which one
    /** \return Pointer to the door, or empty pointer if none exists **/
    const MWWorld::Ptr getNearbyDoor(const MWWorld::Ptr& actor, float minDist);

    class ObstacleCheck
    {
        public:
            ObstacleCheck();

            // Clear the timers and set the state machine to default
            void clear();

            bool isEvading() const;

            /*
                Start of tes3mp addition

                Expose whether the actor should attempt a jump this frame to unstick
                itself. Called by AiPackage::evadeObstacles when strafe evasion has
                failed repeatedly.
            */
            bool shouldJumpToEvade() const;
            /*
                End of tes3mp addition
            */

            // Updates internal state, call each frame for moving actor
            void update(const MWWorld::Ptr& actor, const osg::Vec3f& destination, float duration);

            // change direction to try to fix "stuck" actor
            void takeEvasiveAction(MWMechanics::Movement& actorMovement) const;

        private:
            osg::Vec3f mPrev;

            // directions to try moving in when get stuck
            static const float evadeDirections[NUM_EVADE_DIRECTIONS][2];

            enum class WalkState
            {
                Initial,
                Norm,
                CheckStuck,
                Evade
            };
            WalkState mWalkState;

            float mStateDuration;
            int mEvadeDirectionIndex;
            float mInitialDistance = 0;

            /*
                Start of tes3mp addition

                Track how many strafe evasions in a row have not freed the actor so
                that we can escalate to a jump when lateral movement is not enough
                (e.g. low ledges, rocks, stairs, rubbish in dungeons).
            */
            int mConsecutiveEvadeFailures = 0;
            mutable bool mTriggerJump = false;
            /*
                End of tes3mp addition
            */

            void chooseEvasionDirection();
    };
}

#endif
