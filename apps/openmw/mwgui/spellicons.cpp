#include "spellicons.hpp"

#include <sstream>
#include <iomanip>

#include <MyGUI_ImageBox.h>
#include <MyGUI_Gui.h>

#include <components/esm/loadmgef.hpp>
#include <components/settings/settings.hpp>

#include "../mwbase/world.hpp"
#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/inventorystore.hpp"

#include "../mwmechanics/creaturestats.hpp"
#include "../mwmechanics/actorutil.hpp"

#include "tooltips.hpp"


namespace MWGui
{

    void EffectSourceVisitor::visit (MWMechanics::EffectKey key, int effectIndex,
                                     const std::string& sourceName, const std::string& sourceId, int casterActorId,
                                     float magnitude, float remainingTime, float totalTime)
    {
        MagicEffectInfo newEffectSource;
        newEffectSource.mKey = key;
        newEffectSource.mMagnitude = static_cast<int>(magnitude);
        newEffectSource.mPermanent = mIsPermanent;
        newEffectSource.mRemainingTime = remainingTime;
        newEffectSource.mSource = sourceName;
        newEffectSource.mTotalTime = totalTime;

        mEffectSources[key.mId].push_back(newEffectSource);
    }


    void SpellIcons::updateWidgets(MyGUI::Widget *parent, bool adjustSize)
    {
        // TODO: Tracking add/remove/expire would be better than force updating every frame

        MWWorld::Ptr player = MWMechanics::getPlayer();
        const MWMechanics::CreatureStats& stats = player.getClass().getCreatureStats(player);


        EffectSourceVisitor visitor;

        // permanent item enchantments & permanent spells
        visitor.mIsPermanent = true;
        MWWorld::InventoryStore& store = player.getClass().getInventoryStore(player);
        store.visitEffectSources(visitor);
        stats.getSpells().visitEffectSources(visitor);

        // now add lasting effects
        visitor.mIsPermanent = false;
        stats.getActiveSpells().visitEffectSources(visitor);

        std::map <int, std::vector<MagicEffectInfo> >& effects = visitor.mEffectSources;

        int w=2;

        for (auto& effectInfoPair : effects)
        {
            const int effectId = effectInfoPair.first;
            const ESM::MagicEffect* effect =
                MWBase::Environment::get().getWorld ()->getStore ().get<ESM::MagicEffect>().find(effectId);

            float remainingDuration = 0;
            float totalDuration = 0;

            std::string sourcesDescription;

            static const float fadeTime = MWBase::Environment::get().getWorld()->getStore().get<ESM::GameSetting>().find("fMagicStartIconBlink")->mValue.getFloat();

            std::vector<MagicEffectInfo>& effectInfos = effectInfoPair.second;
            bool addNewLine = false;
            bool anyTimed   = false;  // true if at least one source has a finite duration
            for (const MagicEffectInfo& effectInfo : effectInfos)
            {
                if (addNewLine)
                    sourcesDescription += "\n";

                // if at least one of the effect sources is permanent, the effect will never wear off
                if (effectInfo.mPermanent)
                {
                    remainingDuration = fadeTime;
                    totalDuration = fadeTime;
                }
                else
                {
                    remainingDuration = std::max(remainingDuration, effectInfo.mRemainingTime);
                    totalDuration = std::max(totalDuration, effectInfo.mTotalTime);
                    anyTimed = true;
                }

                sourcesDescription += effectInfo.mSource;

                if (effect->mData.mFlags & ESM::MagicEffect::TargetSkill)
                    sourcesDescription += " (" +
                            MWBase::Environment::get().getWindowManager()->getGameSettingString(
                                ESM::Skill::sSkillNameIds[effectInfo.mKey.mArg], "") + ")";
                if (effect->mData.mFlags & ESM::MagicEffect::TargetAttribute)
                    sourcesDescription += " (" +
                            MWBase::Environment::get().getWindowManager()->getGameSettingString(
                                ESM::Attribute::sGmstAttributeIds[effectInfo.mKey.mArg], "") + ")";

                ESM::MagicEffect::MagnitudeDisplayType displayType = effect->getMagnitudeDisplayType();
                if (displayType == ESM::MagicEffect::MDT_TimesInt)
                {
                    std::string timesInt =  MWBase::Environment::get().getWindowManager()->getGameSettingString("sXTimesINT", "");
                    std::stringstream formatter;
                    formatter << std::fixed << std::setprecision(1) << " " << (effectInfo.mMagnitude / 10.0f) << timesInt;
                    sourcesDescription += formatter.str();
                }
                else if ( displayType != ESM::MagicEffect::MDT_None )
                {
                    sourcesDescription += ": " + MyGUI::utility::toString(effectInfo.mMagnitude);

                    if ( displayType == ESM::MagicEffect::MDT_Percentage )
                        sourcesDescription += MWBase::Environment::get().getWindowManager()->getGameSettingString("spercent", "");
                    else if ( displayType == ESM::MagicEffect::MDT_Feet )
                        sourcesDescription += " " + MWBase::Environment::get().getWindowManager()->getGameSettingString("sfeet", "");
                    else if ( displayType == ESM::MagicEffect::MDT_Level )
                    {
                        sourcesDescription += " " + ((effectInfo.mMagnitude > 1) ?
                            MWBase::Environment::get().getWindowManager()->getGameSettingString("sLevels", "") :
                            MWBase::Environment::get().getWindowManager()->getGameSettingString("sLevel", "") );
                    }
                    else // ESM::MagicEffect::MDT_Points
                    {
                        sourcesDescription += " " + ((effectInfo.mMagnitude > 1) ?
                            MWBase::Environment::get().getWindowManager()->getGameSettingString("spoints", "") :
                            MWBase::Environment::get().getWindowManager()->getGameSettingString("spoint", "") );
                    }
                }
                if (effectInfo.mRemainingTime > -1 && Settings::Manager::getBool("show effect duration","Game"))
                    sourcesDescription += MWGui::ToolTips::getDurationString(effectInfo.mRemainingTime, " #{sDuration}");

                addNewLine = true;
            }

            if (remainingDuration > 0.f)
            {
                MyGUI::ImageBox* image;
                MyGUI::ImageBox* overlay = nullptr;  // <--- cooldown "clock wipe"

                if (mWidgetMap.find(effectId) == mWidgetMap.end())
                {
                    image = parent->createWidget<MyGUI::ImageBox>
                        ("ImageBox", MyGUI::IntCoord(w,2,16,16), MyGUI::Align::Default);
                    mWidgetMap[effectId] = image;

                    image->setImageTexture(MWBase::Environment::get().getWindowManager()->correctIconPath(effect->mIcon));

                    std::string name = ESM::MagicEffect::effectIdToString (effectId);

                    ToolTipInfo tooltipInfo;
                    tooltipInfo.caption = "#{" + name + "}";
                    tooltipInfo.icon = effect->mIcon;
                    tooltipInfo.imageSize = 16;
                    tooltipInfo.wordWrap = false;

                    image->setUserData(tooltipInfo);
                    image->setUserString("ToolTipType", "ToolTipInfo");

                    // ----------------------------------------------------
                    // Create a child overlay that we tint near-black and
                    // use as a "cooldown wipe" — its alpha is driven from
                    // the ratio of remaining to total duration below.
                    // This gives the classic MOBA/RPG "clock filling" feel
                    // without needing a full RotatingSkin setup per icon.
                    // ----------------------------------------------------
                    overlay = image->createWidget<MyGUI::ImageBox>(
                        "ImageBox",
                        MyGUI::IntCoord(0, 0, 16, 16),
                        MyGUI::Align::Stretch);
                    overlay->setColour(MyGUI::Colour(0.f, 0.f, 0.f));
                    overlay->setAlpha(0.f);
                    overlay->setNeedMouseFocus(false);
                    overlay->setUserString("CooldownOverlay", "1");
                }
                else
                {
                    image = mWidgetMap[effectId];
                    // Look up the overlay we attached on creation.
                    if (image->getChildCount() > 0)
                    {
                        MyGUI::Widget* child = image->getChildAt(0);
                        if (child && child->getUserString("CooldownOverlay") == "1")
                            overlay = child->castType<MyGUI::ImageBox>(false);
                    }
                }

                image->setPosition(w,2);
                image->setVisible(true);
                w += 16;

                ToolTipInfo* tooltipInfo = image->getUserData<ToolTipInfo>();
                tooltipInfo->text = sourcesDescription;

                // Fade-out on the very last blink is preserved from the
                // original code so dying effects keep their signature flash.
                if (totalDuration >= fadeTime && fadeTime > 0.f)
                    image->setAlpha(std::min(remainingDuration/fadeTime, 1.f));
                else
                    image->setAlpha(1.f);

                // Update the cooldown overlay's opacity. When a timed effect
                // is fresh (remaining ≈ total) the icon is unobscured; as it
                // runs out the overlay darkens. When ~20% remains we push
                // the overlay up to ~55% black so the effect reads clearly
                // as "about to expire".
                if (overlay)
                {
                    if (anyTimed && totalDuration > 0.f)
                    {
                        const float ratio = remainingDuration / totalDuration;
                        const float clamped = std::max(0.f, std::min(1.f, ratio));
                        // Inverse of ratio — full icon visible at start,
                        // heavy shadow near the end.
                        float alpha = (1.f - clamped) * 0.55f;
                        overlay->setVisible(true);
                        overlay->setAlpha(alpha);
                    }
                    else
                    {
                        overlay->setVisible(false);
                    }
                }
            }
            else if (mWidgetMap.find(effectId) != mWidgetMap.end())
            {
                MyGUI::ImageBox* image = mWidgetMap[effectId];
                image->setVisible(false);
                image->setAlpha(1.f);
            }
        }

        if (adjustSize)
        {
            int s = w + 2;
            if (effects.empty())
                s = 0;
            int diff = parent->getWidth() - s;
            parent->setSize(s, parent->getHeight());
            parent->setPosition(parent->getLeft()+diff, parent->getTop());
        }

        // hide inactive effects
        for (auto& widgetPair : mWidgetMap)
        {
            if (effects.find(widgetPair.first) == effects.end())
                widgetPair.second->setVisible(false);
        }
    }

}
