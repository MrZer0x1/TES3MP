#include "combat.hpp"

#include <components/misc/rng.hpp>
#include <components/settings/settings.hpp>

#include <components/sceneutil/positionattitudetransform.hpp>

/*
    Start of tes3mp addition

    Include additional headers for multiplayer purposes
*/
#include <components/openmw-mp/TimedLog.hpp>
#include "../mwmp/Main.hpp"
#include "../mwmp/LocalPlayer.hpp"
#include "../mwmp/PlayerList.hpp"
#include "../mwmp/CellController.hpp"
#include "../mwmp/MechanicsHelper.hpp"
/*
    End of tes3mp addition
*/

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/soundmanager.hpp"
#include "../mwbase/windowmanager.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/inventorystore.hpp"
#include "../mwworld/esmstore.hpp"

#include "npcstats.hpp"
#include "movement.hpp"
#include "spellcasting.hpp"
#include "spellresistance.hpp"
#include "difficultyscaling.hpp"
#include "actorutil.hpp"
#include "pathfinding.hpp"

namespace
{

float signedAngleRadians (const osg::Vec3f& v1, const osg::Vec3f& v2, const osg::Vec3f& normal)
{
    return std::atan2((normal * (v1 ^ v2)), (v1 * v2));
}

}

namespace MWMechanics
{

    bool applyOnStrikeEnchantment(const MWWorld::Ptr& attacker, const MWWorld::Ptr& victim, const MWWorld::Ptr& object, const osg::Vec3f& hitPosition, const bool fromProjectile)
    {
        std::string enchantmentName = !object.isEmpty() ? object.getClass().getEnchantment(object) : "";
        if (!enchantmentName.empty())
        {
            const ESM::Enchantment* enchantment = MWBase::Environment::get().getWorld()->getStore().get<ESM::Enchantment>().find(
                        enchantmentName);
            if (enchantment->mData.mType == ESM::Enchantment::WhenStrikes)
            {
                MWMechanics::CastSpell cast(attacker, victim, fromProjectile);
                cast.mHitPosition = hitPosition;
                cast.cast(object, false);
                return true;
            }
        }
        return false;
    }

    bool blockMeleeAttack(const MWWorld::Ptr &attacker, const MWWorld::Ptr &blocker, const MWWorld::Ptr &weapon, float damage, float attackStrength)
    {
        if (!blocker.getClass().hasInventoryStore(blocker))
            return false;

        MWMechanics::CreatureStats& blockerStats = blocker.getClass().getCreatureStats(blocker);

        if (blockerStats.getKnockedDown() // Used for both knockout or knockdown
                || blockerStats.getHitRecovery()
                || blockerStats.isParalyzed())
            return false;

        if (!MWBase::Environment::get().getMechanicsManager()->isReadyToBlock(blocker))
            return false;

        MWWorld::InventoryStore& inv = blocker.getClass().getInventoryStore(blocker);
        MWWorld::ContainerStoreIterator shield = inv.getSlot(MWWorld::InventoryStore::Slot_CarriedLeft);
        if (shield == inv.end() || shield->getTypeName() != typeid(ESM::Armor).name())
            return false;

        if (!blocker.getRefData().getBaseNode())
            return false; // shouldn't happen

        float angleDegrees = osg::RadiansToDegrees(
                    signedAngleRadians (
                    (attacker.getRefData().getPosition().asVec3() - blocker.getRefData().getPosition().asVec3()),
                    blocker.getRefData().getBaseNode()->getAttitude() * osg::Vec3f(0,1,0),
                    osg::Vec3f(0,0,1)));

        const MWWorld::Store<ESM::GameSetting>& gmst = MWBase::Environment::get().getWorld()->getStore().get<ESM::GameSetting>();
        if (angleDegrees < gmst.find("fCombatBlockLeftAngle")->mValue.getFloat())
            return false;
        if (angleDegrees > gmst.find("fCombatBlockRightAngle")->mValue.getFloat())
            return false;

        MWMechanics::CreatureStats& attackerStats = attacker.getClass().getCreatureStats(attacker);

        float blockTerm = blocker.getClass().getSkill(blocker, ESM::Skill::Block) + 0.2f * blockerStats.getAttribute(ESM::Attribute::Agility).getModified()
            + 0.1f * blockerStats.getAttribute(ESM::Attribute::Luck).getModified();
        float enemySwing = attackStrength;
        float swingTerm = enemySwing * gmst.find("fSwingBlockMult")->mValue.getFloat() + gmst.find("fSwingBlockBase")->mValue.getFloat();

        float blockerTerm = blockTerm * swingTerm;
        if (blocker.getClass().getMovementSettings(blocker).mPosition[1] <= 0)
            blockerTerm *= gmst.find("fBlockStillBonus")->mValue.getFloat();
        blockerTerm *= blockerStats.getFatigueTerm();

        float attackerSkill = 0;
        if (weapon.isEmpty())
            attackerSkill = attacker.getClass().getSkill(attacker, ESM::Skill::HandToHand);
        else
            attackerSkill = attacker.getClass().getSkill(attacker, weapon.getClass().getEquipmentSkill(weapon));
        float attackerTerm = attackerSkill + 0.2f * attackerStats.getAttribute(ESM::Attribute::Agility).getModified()
                + 0.1f * attackerStats.getAttribute(ESM::Attribute::Luck).getModified();
        attackerTerm *= attackerStats.getFatigueTerm();

        int x = int(blockerTerm - attackerTerm);
        int iBlockMaxChance = gmst.find("iBlockMaxChance")->mValue.getInteger();
        int iBlockMinChance = gmst.find("iBlockMinChance")->mValue.getInteger();
        x = std::min(iBlockMaxChance, std::max(iBlockMinChance, x));

        /*
            Start of tes3mp change (major)

            Only calculate block chance for LocalPlayers and LocalActors; otherwise,
            get the block state from the relevant DedicatedPlayer or DedicatedActor
        */
        mwmp::Attack *localAttack = MechanicsHelper::getLocalAttack(attacker);

        if (localAttack)
        {
            localAttack->block = false;
        }

        mwmp::Attack *dedicatedAttack = MechanicsHelper::getDedicatedAttack(blocker);

        if ((dedicatedAttack && dedicatedAttack->block == true) ||
            Misc::Rng::roll0to99() < x)
        {
            if (localAttack)
            {
                localAttack->block = true;
            }
        /*
            End of tes3mp change (major)
        */

            // Reduce shield durability by incoming damage
            int shieldhealth = shield->getClass().getItemHealth(*shield);
            shieldhealth -= std::min(shieldhealth, int(damage));
            shield->getCellRef().setCharge(shieldhealth);
            if (shieldhealth == 0)
                inv.unequipItem(*shield, blocker);
            // Reduce blocker fatigue
            const float fFatigueBlockBase = gmst.find("fFatigueBlockBase")->mValue.getFloat();
            const float fFatigueBlockMult = gmst.find("fFatigueBlockMult")->mValue.getFloat();
            const float fWeaponFatigueBlockMult = gmst.find("fWeaponFatigueBlockMult")->mValue.getFloat();
            MWMechanics::DynamicStat<float> fatigue = blockerStats.getFatigue();
            float normalizedEncumbrance = blocker.getClass().getNormalizedEncumbrance(blocker);
            normalizedEncumbrance = std::min(1.f, normalizedEncumbrance);
            float fatigueLoss = fFatigueBlockBase + normalizedEncumbrance * fFatigueBlockMult;
            if (!weapon.isEmpty())
                fatigueLoss += weapon.getClass().getWeight(weapon) * attackStrength * fWeaponFatigueBlockMult;
            fatigue.setCurrent(fatigue.getCurrent() - fatigueLoss);
            blockerStats.setFatigue(fatigue);

            blockerStats.setBlock(true);

            if (blocker == getPlayer())
                blocker.getClass().skillUsageSucceeded(blocker, ESM::Skill::Block, 0);

            return true;
        }
        return false;
    }

    bool isNormalWeapon(const MWWorld::Ptr &weapon)
    {
        if (weapon.isEmpty())
            return false;

        const int flags = weapon.get<ESM::Weapon>()->mBase->mData.mFlags;
        bool isSilver = flags & ESM::Weapon::Silver;
        bool isMagical = flags & ESM::Weapon::Magical;
        bool isEnchanted = !weapon.getClass().getEnchantment(weapon).empty();

        return !isSilver && !isMagical && (!isEnchanted || !Settings::Manager::getBool("enchanted weapons are magical", "Game"));
    }

    void resistNormalWeapon(const MWWorld::Ptr &actor, const MWWorld::Ptr& attacker, const MWWorld::Ptr &weapon, float &damage)
    {
        if (damage == 0 || weapon.isEmpty() || !isNormalWeapon(weapon))
            return;

        const MWMechanics::MagicEffects& effects = actor.getClass().getCreatureStats(actor).getMagicEffects();
        const float resistance = effects.get(ESM::MagicEffect::ResistNormalWeapons).getMagnitude() / 100.f;
        const float weakness = effects.get(ESM::MagicEffect::WeaknessToNormalWeapons).getMagnitude() / 100.f;

        damage *= 1.f - std::min(1.f, resistance-weakness);

        if (damage == 0 && attacker == getPlayer())
            MWBase::Environment::get().getWindowManager()->messageBox("#{sMagicTargetResistsWeapons}");
    }

    void applyWerewolfDamageMult(const MWWorld::Ptr &actor, const MWWorld::Ptr &weapon, float &damage)
    {
        if (damage == 0 || weapon.isEmpty() || !actor.getClass().isNpc())
            return;

        const int flags = weapon.get<ESM::Weapon>()->mBase->mData.mFlags;
        bool isSilver = flags & ESM::Weapon::Silver;

        if (isSilver && actor.getClass().getNpcStats(actor).isWerewolf())
        {
            const MWWorld::ESMStore& store = MWBase::Environment::get().getWorld()->getStore();
            damage *= store.get<ESM::GameSetting>().find("fWereWolfSilverWeaponDamageMult")->mValue.getFloat();
        }
    }

    void projectileHit(const MWWorld::Ptr& attacker, const MWWorld::Ptr& victim, MWWorld::Ptr weapon, const MWWorld::Ptr& projectile,
                       const osg::Vec3f& hitPosition, float attackStrength)
    {
        /*
            Start of tes3mp addition

            Ignore projectiles fired by DedicatedPlayers and DedicatedActors

            If fired by LocalPlayers and LocalActors, get the associated LocalAttack and set its type
            to RANGED while also marking it as a hit
        */
        if (mwmp::PlayerList::isDedicatedPlayer(attacker) || mwmp::Main::get().getCellController()->isDedicatedActor(attacker))
            return;

        mwmp::Attack *localAttack = MechanicsHelper::getLocalAttack(attacker);

        if (localAttack)
        {
            localAttack->type = mwmp::Attack::RANGED;
            localAttack->isHit = true;
        }
        /*
            End of tes3mp addition
        */

        MWBase::World *world = MWBase::Environment::get().getWorld();
        const MWWorld::Store<ESM::GameSetting> &gmst = world->getStore().get<ESM::GameSetting>();

        bool validVictim = !victim.isEmpty() && victim.getClass().isActor();

        float damage = 0.f;
        if (validVictim)
        {
            if (attacker == getPlayer())
                MWBase::Environment::get().getWindowManager()->setEnemy(victim);

            int weaponSkill = ESM::Skill::Marksman;
            if (!weapon.isEmpty())
                weaponSkill = weapon.getClass().getEquipmentSkill(weapon);

            int skillValue = attacker.getClass().getSkill(attacker, weapon.getClass().getEquipmentSkill(weapon));

            /*
                Start of tes3mp addition

                Mark this as a successful attack for the associated LocalAttack unless proven otherwise
            */
            if (localAttack)
                localAttack->success = true;
            /*
                End of tes3mp addition
            */

            /*
                Start of dynamic-combat change

                Original behaviour rolled against getHitChance() and the attack
                could completely whiff. Now every shot that the physics layer
                already confirmed as a hit actually lands, unless the target
                rolls a Sanctuary/Chameleon/Invisibility dodge.
            */
            if (rollCombatDodge(attacker, victim))
            {
                if (localAttack)
                    localAttack->success = false;

                victim.getClass().onHit(victim, damage, false, projectile, attacker, osg::Vec3f(), false);
                MWMechanics::reduceWeaponCondition(damage, false, weapon, attacker);
                return;
            }
            /*
                End of dynamic-combat change
            */

            const unsigned char* attack = weapon.get<ESM::Weapon>()->mBase->mData.mChop;
            damage = attack[0] + ((attack[1] - attack[0]) * attackStrength); // Bow/crossbow damage

            // Arrow/bolt damage
            // NB in case of thrown weapons, we are applying the damage twice since projectile == weapon
            attack = projectile.get<ESM::Weapon>()->mBase->mData.mChop;
            damage += attack[0] + ((attack[1] - attack[0]) * attackStrength);

            adjustWeaponDamage(damage, weapon, attacker);
            if (weapon == projectile || Settings::Manager::getBool("only appropriate ammunition bypasses resistance", "Game") || isNormalWeapon(weapon))
                resistNormalWeapon(victim, attacker, projectile, damage);
            applyWerewolfDamageMult(victim, projectile, damage);

            if (attacker == getPlayer())
                attacker.getClass().skillUsageSucceeded(attacker, weaponSkill, 0);

            /*
                Start of dynamic-combat addition

                Skill now scales damage. Very low skill can produce a graze
                (0.5x) instead of a full hit; past skill 75 there's a chance
                of a crit (2x on top of the skill multiplier).
            */
            if (rollGrazingHit(skillValue))
            {
                damage *= 0.5f;
            }
            else
            {
                bool crit = false;
                applySkillDamageBonus(damage, skillValue, crit);
                if (crit && attacker == getPlayer())
                {
                    MWBase::Environment::get().getWindowManager()->messageBox("#{sTargetCriticalStrike}");
                    MWBase::Environment::get().getSoundManager()->playSound3D(victim, "critical damage", 1.0f, 1.0f);
                }
            }
            /*
                End of dynamic-combat addition
            */

            const MWMechanics::AiSequence& sequence = victim.getClass().getCreatureStats(victim).getAiSequence();
            bool unaware = attacker == getPlayer() && !sequence.isInCombat()
                && !MWBase::Environment::get().getMechanicsManager()->awarenessCheck(attacker, victim);
            bool knockedDown = victim.getClass().getCreatureStats(victim).getKnockedDown();
            if (knockedDown || unaware)
            {
                damage *= gmst.find("fCombatKODamageMult")->mValue.getFloat();
                if (!knockedDown)
                    MWBase::Environment::get().getSoundManager()->playSound3D(victim, "critical damage", 1.0f, 1.0f);
            }
        }

        reduceWeaponCondition(damage, validVictim, weapon, attacker);

        // Apply "On hit" effect of the projectile
        bool appliedEnchantment = applyOnStrikeEnchantment(attacker, victim, projectile, hitPosition, true);

        /*
            Start of tes3mp change (minor)

            Track whether the strike enchantment is successful for attacks by the
            LocalPlayer or LocalActors for their projectile
        */
        if (localAttack)
            localAttack->applyAmmoEnchantment = appliedEnchantment;
        /*
            End of tes3mp change (minor)
        */

        if (validVictim)
        {
            // Non-enchanted arrows shot at enemies have a chance to turn up in their inventory
            if (victim != getPlayer() && !appliedEnchantment)
            {
                float fProjectileThrownStoreChance = gmst.find("fProjectileThrownStoreChance")->mValue.getFloat();
                if (Misc::Rng::rollProbability() < fProjectileThrownStoreChance / 100.f)
                    victim.getClass().getContainerStore(victim).add(projectile, 1, victim);
            }

            victim.getClass().onHit(victim, damage, true, projectile, attacker, hitPosition, true);
        }
        /*
            Start of tes3mp addition

            If this is a local attack that had no victim, send a packet for it here
        */
        else if (localAttack)
        {
            localAttack->hitPosition = MechanicsHelper::getPositionFromVector(hitPosition);
            localAttack->shouldSend = true;
        }
        /*
            End of tes3mp addition
        */
    }

    float getHitChance(const MWWorld::Ptr &attacker, const MWWorld::Ptr &victim, int skillValue)
    {
        MWMechanics::CreatureStats &stats = attacker.getClass().getCreatureStats(attacker);
        const MWMechanics::MagicEffects &mageffects = stats.getMagicEffects();

        MWBase::World *world = MWBase::Environment::get().getWorld();
        const MWWorld::Store<ESM::GameSetting> &gmst = world->getStore().get<ESM::GameSetting>();

        float defenseTerm = 0;
        MWMechanics::CreatureStats& victimStats = victim.getClass().getCreatureStats(victim);
        if (victimStats.getFatigue().getCurrent() >= 0)
        {
            // Maybe we should keep an aware state for actors updated every so often instead of testing every time
            bool unaware = (!victimStats.getAiSequence().isInCombat())
                    && (attacker == getPlayer())
                    && (!MWBase::Environment::get().getMechanicsManager()->awarenessCheck(attacker, victim));
            if (!(victimStats.getKnockedDown() ||
                    victimStats.isParalyzed()
                    || unaware ))
            {
                defenseTerm = victimStats.getEvasion();
            }
            defenseTerm += std::min(100.f,
                                    gmst.find("fCombatInvisoMult")->mValue.getFloat() *
                                    victimStats.getMagicEffects().get(ESM::MagicEffect::Chameleon).getMagnitude());
            defenseTerm += std::min(100.f,
                                    gmst.find("fCombatInvisoMult")->mValue.getFloat() *
                                    victimStats.getMagicEffects().get(ESM::MagicEffect::Invisibility).getMagnitude());
        }
        float attackTerm = skillValue +
                          (stats.getAttribute(ESM::Attribute::Agility).getModified() / 5.0f) +
                          (stats.getAttribute(ESM::Attribute::Luck).getModified() / 10.0f);
        attackTerm *= stats.getFatigueTerm();
        attackTerm += mageffects.get(ESM::MagicEffect::FortifyAttack).getMagnitude() -
                     mageffects.get(ESM::MagicEffect::Blind).getMagnitude();

        return round(attackTerm - defenseTerm);
    }

    /*
        Start of dynamic-combat addition

        Sanctuary / Chameleon / Invisibility still matter in the new system:
        they give the defender a flat percentage chance to dodge. Without this,
        those spells become worthless now that "always hit" is on.
    */
    bool rollCombatDodge(const MWWorld::Ptr &attacker, const MWWorld::Ptr &victim)
    {
        if (victim.isEmpty() || !victim.getClass().isActor())
            return false;

        MWMechanics::CreatureStats &victimStats = victim.getClass().getCreatureStats(victim);

        // Knocked-down / paralyzed targets can't dodge, regardless of buffs.
        if (victimStats.getKnockedDown() || victimStats.isParalyzed())
            return false;

        const MWMechanics::MagicEffects &victimEffects = victimStats.getMagicEffects();
        float dodgeChance = 0.f;
        dodgeChance += victimEffects.get(ESM::MagicEffect::Sanctuary).getMagnitude();
        dodgeChance += 0.25f * victimEffects.get(ESM::MagicEffect::Chameleon).getMagnitude();
        dodgeChance += 0.5f  * victimEffects.get(ESM::MagicEffect::Invisibility).getMagnitude();

        // Blinded attackers lose some accuracy too. Attacker may be empty
        // (e.g. projectile whose caster got destroyed), so guard it.
        if (!attacker.isEmpty() && attacker.getClass().isActor())
        {
            MWMechanics::CreatureStats &attackerStats = attacker.getClass().getCreatureStats(attacker);
            dodgeChance += 0.5f * attackerStats.getMagicEffects().get(ESM::MagicEffect::Blind).getMagnitude();
        }

        if (dodgeChance <= 0.f)
            return false;

        // Cap: even invisible targets don't become 100% untouchable.
        if (dodgeChance > 90.f)
            dodgeChance = 90.f;

        return Misc::Rng::roll0to99() < static_cast<int>(dodgeChance);
    }

    /*
        Very low-skill attackers still sometimes produce glancing hits that
        do half damage, so the "always hit" rule doesn't completely erase
        the penalty of being unskilled.
        Threshold: below skill 20 there is a linear chance of grazing,
        maxing at ~25% at skill 0.
    */
    bool rollGrazingHit(int skillValue)
    {
        if (skillValue >= 20)
            return false;
        // skill 0 -> 25% graze, skill 20 -> 0%
        int grazeChance = static_cast<int>((20 - skillValue) * 1.25f);
        return Misc::Rng::roll0to99() < grazeChance;
    }

    /*
        Applies the skill-as-multiplier and crit system.
        - skillMult: 0.5x at skill 0, 1.5x at 100, 2.5x at 200.
        - critChance: 0% up to skill 75, linearly to 100% at skill 200.
        - Crit multiplier: x2.0 on top of skillMult.
        outCrit is set so the caller can play a sound / show a message.
    */
    void applySkillDamageBonus(float &damage, int skillValue, bool &outCrit)
    {
        outCrit = false;
        if (damage <= 0.f)
            return;

        // Clamp to keep math sane even if something gets weird buffs.
        float clampedSkill = static_cast<float>(std::max(0, std::min(skillValue, 300)));

        float skillMult = 0.5f + (clampedSkill / 100.0f);
        damage *= skillMult;

        if (clampedSkill > 75.f)
        {
            float critChance = (clampedSkill - 75.f) / 125.f; // 0..1
            if (critChance > 1.f) critChance = 1.f;
            if (Misc::Rng::rollProbability() < critChance)
            {
                damage *= 2.0f;
                outCrit = true;
            }
        }
    }

    /*
        Stamina-absorb for physical damage (point 3).

        Design:
          - The victim's fatigue takes the hit first, clamped at -10.
          - Remainder (if any) spills into health via the caller.
          - If fatigue is already at -10 or below when the hit lands,
            all of the damage goes through to health.

        Rationale for the -10 floor: if we let fatigue drain arbitrarily,
        a single strong hit would use up both stamina *and* still leave
        the victim with very negative fatigue afterwards (harder to stand
        up / act). Cap at -10 keeps the "exhausted but not comatose" feel.
    */
    float absorbPhysicalDamageWithStamina(const MWWorld::Ptr &victim, float damage)
    {
        if (damage <= 0.f || victim.isEmpty() || !victim.getClass().isActor())
            return damage;

        const float kFatigueFloor = -10.f;

        MWMechanics::CreatureStats &stats = victim.getClass().getCreatureStats(victim);
        MWMechanics::DynamicStat<float> fatigue = stats.getFatigue();
        float fatigueCurrent = fatigue.getCurrent();

        // Already below the floor? no stamina shield.
        if (fatigueCurrent <= kFatigueFloor)
            return damage;

        float absorbCapacity = fatigueCurrent - kFatigueFloor; // always > 0 here
        float absorbed       = std::min(damage, absorbCapacity);
        float newFatigue     = fatigueCurrent - absorbed;
        // setCurrent with allowDecreaseBelowZero=true; we've already clamped above.
        fatigue.setCurrent(newFatigue, true);
        stats.setFatigue(fatigue);

        return damage - absorbed;
    }

    /*
        Magicka-absorb for magical damage (point 4).

        Design:
          - If magicka > 0: magicka drains first, the rest of the damage
            (if any) spills into health 1:1.
          - If magicka is already at 0 or below when the hit arrives,
            the damage to health is doubled ("no shield left, full exposure").
          - Magicka is clamped at 0 on the low end - we don't want negative
            magicka (that would break regen logic).

        Caller is responsible for godmode checks and for actually applying
        the returned value to health.
    */
    float absorbMagicalDamageWithMagicka(const MWWorld::Ptr &victim, float damage)
    {
        if (damage <= 0.f || victim.isEmpty() || !victim.getClass().isActor())
            return damage;

        MWMechanics::CreatureStats &stats = victim.getClass().getCreatureStats(victim);
        MWMechanics::DynamicStat<float> magicka = stats.getMagicka();
        float magickaCurrent = magicka.getCurrent();

        if (magickaCurrent <= 0.f)
        {
            // No magicka shield -> double damage to health.
            return damage * 2.0f;
        }

        float absorbed      = std::min(damage, magickaCurrent);
        float newMagicka    = magickaCurrent - absorbed;
        magicka.setCurrent(newMagicka); // default: clamps at 0
        stats.setMagicka(magicka);

        return damage - absorbed;
    }
    /*
        End of dynamic-combat addition
    */

    void applyElementalShields(const MWWorld::Ptr &attacker, const MWWorld::Ptr &victim)
    {
        // Don't let elemental shields harm the player in god mode.
        bool godmode = attacker == getPlayer() && MWBase::Environment::get().getWorld()->getGodModeState();
        if (godmode)
            return;
        for (int i=0; i<3; ++i)
        {
            float magnitude = victim.getClass().getCreatureStats(victim).getMagicEffects().get(ESM::MagicEffect::FireShield+i).getMagnitude();

            if (!magnitude)
                continue;

            CreatureStats& attackerStats = attacker.getClass().getCreatureStats(attacker);
            float saveTerm = attacker.getClass().getSkill(attacker, ESM::Skill::Destruction)
                    + 0.2f * attackerStats.getAttribute(ESM::Attribute::Willpower).getModified()
                    + 0.1f * attackerStats.getAttribute(ESM::Attribute::Luck).getModified();

            float fatigueMax = attackerStats.getFatigue().getModified();
            float fatigueCurrent = attackerStats.getFatigue().getCurrent();

            float normalisedFatigue = floor(fatigueMax)==0 ? 1 : std::max (0.0f, (fatigueCurrent/fatigueMax));

            saveTerm *= 1.25f * normalisedFatigue;

            float x = std::max(0.f, saveTerm - Misc::Rng::roll0to99());

            int element = ESM::MagicEffect::FireDamage;
            if (i == 1)
                element = ESM::MagicEffect::ShockDamage;
            if (i == 2)
                element = ESM::MagicEffect::FrostDamage;

            float elementResistance = MWMechanics::getEffectResistanceAttribute(element, &attackerStats.getMagicEffects());

            x = std::min(100.f, x + elementResistance);

            static const float fElementalShieldMult = MWBase::Environment::get().getWorld()->getStore().get<ESM::GameSetting>().find("fElementalShieldMult")->mValue.getFloat();
            x = fElementalShieldMult * magnitude * (1.f - 0.01f * x);

            // Note swapped victim and attacker, since the attacker takes the damage here.
            x = scaleDamage(x, victim, attacker);

            MWMechanics::DynamicStat<float> health = attackerStats.getHealth();
            health.setCurrent(health.getCurrent() - x);
            attackerStats.setHealth(health);

            MWBase::Environment::get().getSoundManager()->playSound3D(attacker, "Health Damage", 1.0f, 1.0f);
        }
    }

    void reduceWeaponCondition(float damage, bool hit, MWWorld::Ptr &weapon, const MWWorld::Ptr &attacker)
    {
        if (weapon.isEmpty())
            return;

        if (!hit)
            damage = 0.f;

        const bool weaphashealth = weapon.getClass().hasItemHealth(weapon);
        if(weaphashealth)
        {
            int weaphealth = weapon.getClass().getItemHealth(weapon);

            bool godmode = attacker == MWMechanics::getPlayer() && MWBase::Environment::get().getWorld()->getGodModeState();

            // weapon condition does not degrade when godmode is on
            if (!godmode)
            {
                const float fWeaponDamageMult = MWBase::Environment::get().getWorld()->getStore().get<ESM::GameSetting>().find("fWeaponDamageMult")->mValue.getFloat();
                float x = std::max(1.f, fWeaponDamageMult * damage);

                weaphealth -= std::min(int(x), weaphealth);
                weapon.getCellRef().setCharge(weaphealth);
            }

            // Weapon broken? unequip it
            if (weaphealth == 0)
                weapon = *attacker.getClass().getInventoryStore(attacker).unequipItem(weapon, attacker);
        }
    }

    void adjustWeaponDamage(float &damage, const MWWorld::Ptr &weapon, const MWWorld::Ptr& attacker)
    {
        if (weapon.isEmpty())
            return;

        const bool weaphashealth = weapon.getClass().hasItemHealth(weapon);
        if (weaphashealth)
        {
            damage *= weapon.getClass().getItemNormalizedHealth(weapon);
        }

        static const float fDamageStrengthBase = MWBase::Environment::get().getWorld()->getStore().get<ESM::GameSetting>()
                .find("fDamageStrengthBase")->mValue.getFloat();
        static const float fDamageStrengthMult = MWBase::Environment::get().getWorld()->getStore().get<ESM::GameSetting>()
                .find("fDamageStrengthMult")->mValue.getFloat();
        damage *= fDamageStrengthBase +
                (attacker.getClass().getCreatureStats(attacker).getAttribute(ESM::Attribute::Strength).getModified() * fDamageStrengthMult * 0.1f);
    }

    void getHandToHandDamage(const MWWorld::Ptr &attacker, const MWWorld::Ptr &victim, float &damage, bool &healthdmg, float attackStrength)
    {
        const MWWorld::ESMStore& store = MWBase::Environment::get().getWorld()->getStore();
        float minstrike = store.get<ESM::GameSetting>().find("fMinHandToHandMult")->mValue.getFloat();
        float maxstrike = store.get<ESM::GameSetting>().find("fMaxHandToHandMult")->mValue.getFloat();
        damage  = static_cast<float>(attacker.getClass().getSkill(attacker, ESM::Skill::HandToHand));
        damage *= minstrike + ((maxstrike-minstrike)*attackStrength);

        MWMechanics::CreatureStats& otherstats = victim.getClass().getCreatureStats(victim);
        healthdmg = otherstats.isParalyzed()
                || otherstats.getKnockedDown();
        bool isWerewolf = (attacker.getClass().isNpc() && attacker.getClass().getNpcStats(attacker).isWerewolf());

        // Options in the launcher's combo box: unarmedFactorsStrengthComboBox
        // 0 = Do not factor strength into hand-to-hand combat.
        // 1 = Factor into werewolf hand-to-hand combat.
        // 2 = Ignore werewolves.
        int factorStrength = Settings::Manager::getInt("strength influences hand to hand", "Game");
        if (factorStrength == 1 || (factorStrength == 2 && !isWerewolf)) {
            damage *= attacker.getClass().getCreatureStats(attacker).getAttribute(ESM::Attribute::Strength).getModified() / 40.0f;
        }

        if(isWerewolf)
        {
            healthdmg = true;
            // GLOB instead of GMST because it gets updated during a quest
            damage *= MWBase::Environment::get().getWorld()->getGlobalFloat("werewolfclawmult");
        }
        if(healthdmg)
            damage *= store.get<ESM::GameSetting>().find("fHandtoHandHealthPer")->mValue.getFloat();

        MWBase::SoundManager *sndMgr = MWBase::Environment::get().getSoundManager();
        if(isWerewolf)
        {
            const ESM::Sound *sound = store.get<ESM::Sound>().searchRandom("WolfHit");
            if(sound)
                sndMgr->playSound3D(victim, sound->mId, 1.0f, 1.0f);
        }
        else if (!healthdmg)
            sndMgr->playSound3D(victim, "Hand To Hand Hit", 1.0f, 1.0f);
    }

    void applyFatigueLoss(const MWWorld::Ptr &attacker, const MWWorld::Ptr &weapon, float attackStrength)
    {
        // somewhat of a guess, but using the weapon weight makes sense
        const MWWorld::Store<ESM::GameSetting>& store = MWBase::Environment::get().getWorld()->getStore().get<ESM::GameSetting>();
        const float fFatigueAttackBase = store.find("fFatigueAttackBase")->mValue.getFloat();
        const float fFatigueAttackMult = store.find("fFatigueAttackMult")->mValue.getFloat();
        const float fWeaponFatigueMult = store.find("fWeaponFatigueMult")->mValue.getFloat();
        CreatureStats& stats = attacker.getClass().getCreatureStats(attacker);
        MWMechanics::DynamicStat<float> fatigue = stats.getFatigue();
        const float normalizedEncumbrance = attacker.getClass().getNormalizedEncumbrance(attacker);

        bool godmode = attacker == MWMechanics::getPlayer() && MWBase::Environment::get().getWorld()->getGodModeState();

        if (!godmode)
        {
            float fatigueLoss = fFatigueAttackBase + normalizedEncumbrance * fFatigueAttackMult;
            if (!weapon.isEmpty())
                fatigueLoss += weapon.getClass().getWeight(weapon) * attackStrength * fWeaponFatigueMult;
            fatigue.setCurrent(fatigue.getCurrent() - fatigueLoss);
            stats.setFatigue(fatigue);
        }
    }

    float getFightDistanceBias(const MWWorld::Ptr& actor1, const MWWorld::Ptr& actor2)
    {
        osg::Vec3f pos1 (actor1.getRefData().getPosition().asVec3());
        osg::Vec3f pos2 (actor2.getRefData().getPosition().asVec3());

        float d = getAggroDistance(actor1, pos1, pos2);

        static const int iFightDistanceBase = MWBase::Environment::get().getWorld()->getStore().get<ESM::GameSetting>().find(
                    "iFightDistanceBase")->mValue.getInteger();
        static const float fFightDistanceMultiplier = MWBase::Environment::get().getWorld()->getStore().get<ESM::GameSetting>().find(
                    "fFightDistanceMultiplier")->mValue.getFloat();

        return (iFightDistanceBase - fFightDistanceMultiplier * d);
    }

    bool isTargetMagicallyHidden(const MWWorld::Ptr& target)
    {
        const MagicEffects& magicEffects = target.getClass().getCreatureStats(target).getMagicEffects();
        return (magicEffects.get(ESM::MagicEffect::Invisibility).getMagnitude() > 0)
            || (magicEffects.get(ESM::MagicEffect::Chameleon).getMagnitude() > 75);
    }

    float getAggroDistance(const MWWorld::Ptr& actor, const osg::Vec3f& lhs, const osg::Vec3f& rhs)
    {
        if (canActorMoveByZAxis(actor))
            return distanceIgnoreZ(lhs, rhs);
        return distance(lhs, rhs);
    }
}
