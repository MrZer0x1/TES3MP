#ifndef OPENMW_MECHANICS_COMBAT_H
#define OPENMW_MECHANICS_COMBAT_H

namespace osg
{
    class Vec3f;
}

namespace MWWorld
{
    class Ptr;
}

namespace MWMechanics
{

bool applyOnStrikeEnchantment(const MWWorld::Ptr& attacker, const MWWorld::Ptr& victim, const MWWorld::Ptr& object, const osg::Vec3f& hitPosition,
                              const bool fromProjectile=false);

/// @return can we block the attack?
bool blockMeleeAttack (const MWWorld::Ptr& attacker, const MWWorld::Ptr& blocker, const MWWorld::Ptr& weapon, float damage, float attackStrength);

/// @return does normal weapon resistance and weakness apply to the weapon?
bool isNormalWeapon (const MWWorld::Ptr& weapon);

void resistNormalWeapon (const MWWorld::Ptr& actor, const MWWorld::Ptr& attacker, const MWWorld::Ptr& weapon, float& damage);

void applyWerewolfDamageMult (const MWWorld::Ptr& actor, const MWWorld::Ptr& weapon, float &damage);

/// @note for a thrown weapon, \a weapon == \a projectile, for bows/crossbows, \a projectile is the arrow/bolt
/// @note \a victim may be empty (e.g. for a hit on terrain), a non-actor (environment objects) or an actor
void projectileHit (const MWWorld::Ptr& attacker, const MWWorld::Ptr& victim, MWWorld::Ptr weapon, const MWWorld::Ptr& projectile,
                    const osg::Vec3f& hitPosition, float attackStrength);

/// Get the chance (in percent) for \a attacker to successfully hit \a victim with a given weapon skill value
float getHitChance (const MWWorld::Ptr& attacker, const MWWorld::Ptr& victim, int skillValue);

/*
    Start of dynamic-combat addition

    Helpers for the reworked combat system:
      - rollCombatDodge: Sanctuary/Chameleon/Invisibility still let the victim
        avoid a hit even though base accuracy is 100%. Returns true if dodged.
      - rollGrazingHit: low-skill attackers still sometimes land weak hits
        instead of every swing dealing full damage. Returns true for a graze.
      - applySkillDamageBonus: scales damage by weapon skill (0.5x at 0,
        1.5x at 100, 2.5x at 200) and rolls a crit chance that becomes
        meaningful past skill 75 and reaches 100% at 200.
      - absorbPhysicalDamageWithStamina: point 3. Stamina eats the hit
        first down to -10, then the rest goes to health.
      - absorbMagicalDamageWithMagicka: point 4. Magicka eats magic
        damage first; if magicka is empty the incoming hit is doubled
        against health.
*/
bool rollCombatDodge (const MWWorld::Ptr& attacker, const MWWorld::Ptr& victim);
bool rollGrazingHit (int skillValue);
void applySkillDamageBonus (float& damage, int skillValue, bool& outCrit);

/*
    Stamina-absorb for physical damage (point 3).
    Drains fatigue first; remaining damage is returned for health application.
    Fatigue is clamped at FATIGUE_FLOOR below (default -10).
    Returns the amount of damage that should still be applied to health.
*/
float absorbPhysicalDamageWithStamina (const MWWorld::Ptr& victim, float damage);

/*
    Magicka-absorb for magical damage (point 4).
    Drains magicka first; remaining damage goes to health. If magicka is
    already empty before the hit, damage is doubled before being returned
    ("no shield left, full exposure").
    Returns the amount of damage that should still be applied to health.
*/
float absorbMagicalDamageWithMagicka (const MWWorld::Ptr& victim, float damage);
/*
    End of dynamic-combat addition
*/

/// Applies damage to attacker based on the victim's elemental shields.
void applyElementalShields(const MWWorld::Ptr& attacker, const MWWorld::Ptr& victim);

/// @param damage Unmitigated weapon damage of the attack
/// @param hit Was the attack successful?
/// @param weapon The weapon used.
/// @note if the weapon is unequipped as result of condition damage, a new Ptr will be assigned to \a weapon.
void reduceWeaponCondition (float damage, bool hit, MWWorld::Ptr& weapon, const MWWorld::Ptr& attacker);

/// Adjust weapon damage based on its condition. A used weapon will be less effective.
void adjustWeaponDamage (float& damage, const MWWorld::Ptr& weapon, const MWWorld::Ptr& attacker);

void getHandToHandDamage (const MWWorld::Ptr& attacker, const MWWorld::Ptr& victim, float& damage, bool& healthdmg, float attackStrength);

/// Apply the fatigue loss incurred by attacking with the given weapon (weapon may be empty = hand-to-hand)
void applyFatigueLoss(const MWWorld::Ptr& attacker, const MWWorld::Ptr& weapon, float attackStrength);

float getFightDistanceBias(const MWWorld::Ptr& actor1, const MWWorld::Ptr& actor2);

bool isTargetMagicallyHidden(const MWWorld::Ptr& target);

float getAggroDistance(const MWWorld::Ptr& actor, const osg::Vec3f& lhs, const osg::Vec3f& rhs);

}

#endif
