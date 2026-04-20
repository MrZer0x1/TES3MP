#include "hud.hpp"

#include <MyGUI_RenderManager.h>
#include <MyGUI_ProgressBar.h>
#include <MyGUI_Button.h>
#include <MyGUI_InputManager.h>
#include <MyGUI_ImageBox.h>
#include <MyGUI_ScrollView.h>
#include <MyGUI_EditBox.h>
#include <MyGUI_Gui.h>
#include <MyGUI_TextBox.h>

#include <cmath>
#include <iomanip>
#include <sstream>

/*
    Start of tes3mp addition

    Include additional headers for multiplayer purposes
*/
#include "../mwmp/Main.hpp"
#include "../mwmp/Networking.hpp"
#include "../mwmp/ObjectList.hpp"
#include "../mwworld/cellstore.hpp"
/*
    End of tes3mp addition
*/

#include <components/settings/settings.hpp>
#include <components/openmw-mp/TimedLog.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/esmstore.hpp"

#include "../mwmechanics/creaturestats.hpp"
#include "../mwmechanics/npcstats.hpp"
#include "../mwmechanics/actorutil.hpp"
#include "../mwmechanics/drawstate.hpp"

#include "inventorywindow.hpp"
#include "spellicons.hpp"
#include "itemmodel.hpp"
#include "draganddrop.hpp"

#include "itemwidget.hpp"

namespace MWGui
{

    /**
     * Makes it possible to use ItemModel::moveItem to move an item from an inventory to the world.
     */
    class WorldItemModel : public ItemModel
    {
    public:
        WorldItemModel(float left, float top) : mLeft(left), mTop(top) {}
        virtual ~WorldItemModel() override {}
        MWWorld::Ptr copyItem (const ItemStack& item, size_t count, bool /*allowAutoEquip*/) override
        {
            MWBase::World* world = MWBase::Environment::get().getWorld();

            MWWorld::Ptr dropped;
            if (world->canPlaceObject(mLeft, mTop))
                dropped = world->placeObject(item.mBase, mLeft, mTop, count);
            else
                dropped = world->dropObjectOnGround(world->getPlayerPtr(), item.mBase, count);
            dropped.getCellRef().setOwner("");

            /*
                Start of tes3mp addition

                Send an ID_OBJECT_PLACE packet every time an object is dropped into the world from
                the inventory screen
            */
            mwmp::ObjectList *objectList = mwmp::Main::get().getNetworking()->getObjectList();
            objectList->reset();
            objectList->packetOrigin = mwmp::CLIENT_GAMEPLAY;
            objectList->addObjectPlace(dropped, true);
            objectList->sendObjectPlace();
            /*
                End of tes3mp addition
            */

            /*
                Start of tes3mp change (major)

                Instead of actually keeping this object as is, delete it after sending the packet
                and wait for the server to send it back with a unique mpNum of its own
            */
            MWBase::Environment::get().getWorld()->deleteObject(dropped);
            /*
                End of tes3mp change (major)
            */

            return dropped;
        }

        void removeItem (const ItemStack& item, size_t count) override { throw std::runtime_error("removeItem not implemented"); }
        ModelIndex getIndex (ItemStack item) override { throw std::runtime_error("getIndex not implemented"); }
        void update() override {}
        size_t getItemCount() override { return 0; }
        ItemStack getItem (ModelIndex index) override { throw std::runtime_error("getItem not implemented"); }

    private:
        // Where to drop the item
        float mLeft;
        float mTop;
    };


    HUD::HUD(CustomMarkerCollection &customMarkers, DragAndDrop* dragAndDrop, MWRender::LocalMap* localMapRender)
        : WindowBase("openmw_hud.layout")
        , LocalMapBase(customMarkers, localMapRender, Settings::Manager::getBool("local map hud fog of war", "Map"))
        , mHealth(nullptr)
        , mMagicka(nullptr)
        , mStamina(nullptr)
        , mDrowning(nullptr)
        , mHealthText(nullptr)
        , mMagickaText(nullptr)
        , mStaminaText(nullptr)
        , mFpsBox(nullptr)
        , mWeapImage(nullptr)
        , mSpellImage(nullptr)
        , mWeapStatus(nullptr)
        , mSpellStatus(nullptr)
        , mEffectBox(nullptr)
        , mMinimap(nullptr)
        , mCrosshair(nullptr)
        , mCellNameBox(nullptr)
        , mGameTimeBox(nullptr)
        , mDrowningFrame(nullptr)
        , mDrowningFlash(nullptr)
        , mChatPanel(nullptr)
        , mChatHistoryView(nullptr)
        , mChatHistory(nullptr)
        , mChatCommand(nullptr)
        , mAllyFramesBox(nullptr)
        , mHealthManaStaminaBaseLeft(0)
        , mWeapBoxBaseLeft(0)
        , mSpellBoxBaseLeft(0)
        , mMinimapBoxBaseRight(0)
        , mEffectBoxBaseRight(0)
        , mDragAndDrop(dragAndDrop)
        , mCellNameTimer(0.0f)
        , mWeaponSpellTimer(0.f)
        , mGameTimeUpdateTimer(0.f)
        , mMapVisible(true)
        , mWeaponVisible(true)
        , mSpellVisible(true)
        , mWorldMouseOver(false)
        , mEnemyActorId(-1)
        , mEnemyHealthTimer(-1)
        , mFpsUpdateTimer(0.f)
        , mFpsAccumulatedTime(0.f)
        , mFpsFrameCount(0)
        , mIsDrowning(false)
        , mDrowningFlashTheta(0.f)
        , mHmsBaseVisible(true)
        , mLastDrawState(-1)
    {
        mMainWidget->setSize(MyGUI::RenderManager::getInstance().getViewSize());

        // Energy bars
        getWidget(mHealthFrame, "HealthFrame");
        getWidget(mMagickaFrame, "MagickaFrame");
        getWidget(mFatigueFrame, "FatigueFrame");
        getWidget(mHealth, "Health");
        getWidget(mMagicka, "Magicka");
        getWidget(mStamina, "Stamina");
        getWidget(mEnemyHealth, "EnemyHealth");
        getWidget(mHealthText, "HealthText");
        getWidget(mMagickaText, "MagickaText");
        getWidget(mStaminaText, "StaminaText");
        getWidget(mFpsBox, "FpsText");
        mHealthManaStaminaBaseLeft = mHealthFrame->getLeft();

        mHealthFrame->eventMouseButtonClick += MyGUI::newDelegate(this, &HUD::onHMSClicked);
        mMagickaFrame->eventMouseButtonClick += MyGUI::newDelegate(this, &HUD::onHMSClicked);
        mFatigueFrame->eventMouseButtonClick += MyGUI::newDelegate(this, &HUD::onHMSClicked);

        //Drowning bar
        getWidget(mDrowningFrame, "DrowningFrame");
        getWidget(mDrowning, "Drowning");
        getWidget(mDrowningFlash, "Flash");
        mDrowning->setProgressRange(200);

        const MyGUI::IntSize& viewSize = MyGUI::RenderManager::getInstance().getViewSize();

        // Item and spell images and status bars
        getWidget(mWeapBox, "WeapBox");
        getWidget(mWeapImage, "WeapImage");
        getWidget(mWeapStatus, "WeapStatus");
        mWeapBoxBaseLeft = mWeapBox->getLeft();
        mWeapBox->eventMouseButtonClick += MyGUI::newDelegate(this, &HUD::onWeaponClicked);

        getWidget(mSpellBox, "SpellBox");
        getWidget(mSpellImage, "SpellImage");
        getWidget(mSpellStatus, "SpellStatus");
        mSpellBoxBaseLeft = mSpellBox->getLeft();
        mSpellBox->eventMouseButtonClick += MyGUI::newDelegate(this, &HUD::onMagicClicked);

        getWidget(mSneakBox, "SneakBox");
        mSneakBoxBaseLeft = mSneakBox->getLeft();

        getWidget(mEffectBox, "EffectBox");
        mEffectBoxBaseRight = viewSize.width - mEffectBox->getRight();

        getWidget(mMinimapBox, "MiniMapBox");
        mMinimapBoxBaseRight = viewSize.width - mMinimapBox->getRight();
        getWidget(mMinimap, "MiniMap");
        getWidget(mCompass, "Compass");
        getWidget(mMinimapButton, "MiniMapButton");
        mMinimapButton->eventMouseButtonClick += MyGUI::newDelegate(this, &HUD::onMapClicked);

        getWidget(mCellNameBox, "CellName");
        getWidget(mWeaponSpellBox, "WeaponSpellName");
        getWidget(mGameTimeBox, "GameTime");

        getWidget(mCrosshair, "Crosshair");

        // --- TES3MP modern HUD: grab the embedded chat + ally widgets.   --
        // These may be absent if the user is running an older layout, so   --
        // everything below is guarded against nullptr by probing with      --
        // findWidget() before calling the throwing getWidget<T>() helper.  --
        auto tryGet = [this](const std::string& name) -> MyGUI::Widget* {
            return mMainWidget->findWidget(name);
        };

        if (tryGet("ChatPanel"))        getWidget(mChatPanel,       "ChatPanel");
        if (tryGet("ChatHistoryView"))  getWidget(mChatHistoryView, "ChatHistoryView");
        if (tryGet("ChatHistory"))      getWidget(mChatHistory,     "ChatHistory");
        if (tryGet("ChatCommand"))      getWidget(mChatCommand,     "ChatCommand");
        if (tryGet("AllyFramesBox"))    getWidget(mAllyFramesBox,   "AllyFramesBox");

        // The chat panel starts hidden; GUIChat toggles it via its own mode state.
        if (mChatPanel)
        {
            mChatPanel->setAlpha(0.85f); // semi-transparent MMO-style panel
            mChatPanel->setVisible(false);
        }
        if (mChatCommand)
            mChatCommand->setVisible(false);
        // ------------------------------------------------------------------

        LocalMapBase::init(mMinimap, mCompass);

        mMainWidget->eventMouseButtonClick += MyGUI::newDelegate(this, &HUD::onWorldClicked);
        mMainWidget->eventMouseMove += MyGUI::newDelegate(this, &HUD::onWorldMouseOver);
        mMainWidget->eventMouseLostFocus += MyGUI::newDelegate(this, &HUD::onWorldMouseLostFocus);

        mSpellIcons = new SpellIcons();
    }

    HUD::~HUD()
    {
        mMainWidget->eventMouseLostFocus.clear();
        mMainWidget->eventMouseMove.clear();
        mMainWidget->eventMouseButtonClick.clear();

        delete mSpellIcons;
    }

    void HUD::setValue(const std::string& id, const MWMechanics::DynamicStat<float>& value)
    {
        int current = static_cast<int>(value.getCurrent());
        int modified = static_cast<int>(value.getModified());

        // Fatigue can be negative
        if (id != "FBar")
            current = std::max(0, current);

        std::string valStr = MyGUI::utility::toString(current) + " / " + MyGUI::utility::toString(modified);
        if (id == "HBar")
        {
            mHealth->setProgressRange(std::max(0, modified));
            mHealth->setProgressPosition(std::max(0, current));
            if (mHealthText)
                mHealthText->setCaption(valStr);
            mHealthFrame->setUserString("Caption_HealthDescription", "#{sHealthDesc}\n" + valStr);
            registerBarChange(mHealthBarState, current, modified);
        }
        else if (id == "MBar")
        {
            mMagicka->setProgressRange(std::max(0, modified));
            mMagicka->setProgressPosition(std::max(0, current));
            if (mMagickaText)
                mMagickaText->setCaption(valStr);
            mMagickaFrame->setUserString("Caption_HealthDescription", "#{sMagDesc}\n" + valStr);
            registerBarChange(mMagickaBarState, current, modified);
        }
        else if (id == "FBar")
        {
            mStamina->setProgressRange(std::max(0, modified));
            mStamina->setProgressPosition(std::max(0, current));
            if (mStaminaText)
                mStaminaText->setCaption(valStr);
            mFatigueFrame->setUserString("Caption_HealthDescription", "#{sFatDesc}\n" + valStr);
            registerBarChange(mStaminaBarState, current, modified);
        }
    }

    void HUD::setDrowningTimeLeft(float time, float maxTime)
    {
        size_t progress = static_cast<size_t>(time / maxTime * 200);
        mDrowning->setProgressPosition(progress);

        bool isDrowning = (progress == 0);
        if (isDrowning && !mIsDrowning) // Just started drowning
            mDrowningFlashTheta = 0.0f; // Start out on bright red every time.

        mDrowningFlash->setVisible(isDrowning);
        mIsDrowning = isDrowning;
    }

    void HUD::setDrowningBarVisible(bool visible)
    {
        mDrowningFrame->setVisible(visible);
    }

    void HUD::onWorldClicked(MyGUI::Widget* _sender)
    {
        if (!MWBase::Environment::get().getWindowManager ()->isGuiMode ())
            return;

        MWBase::WindowManager *winMgr = MWBase::Environment::get().getWindowManager();
        if (mDragAndDrop->mIsOnDragAndDrop)
        {
            // drop item into the gameworld
            MWBase::Environment::get().getWorld()->breakInvisibility(
                        MWMechanics::getPlayer());

            MyGUI::IntSize viewSize = MyGUI::RenderManager::getInstance().getViewSize();
            MyGUI::IntPoint cursorPosition = MyGUI::InputManager::getInstance().getMousePosition();
            float mouseX = cursorPosition.left / float(viewSize.width);
            float mouseY = cursorPosition.top / float(viewSize.height);

            WorldItemModel drop (mouseX, mouseY);
            mDragAndDrop->drop(&drop, nullptr);

            winMgr->changePointer("arrow");
        }
        else
        {
            GuiMode mode = winMgr->getMode();

            if (!winMgr->isConsoleMode() && (mode != GM_Container) && (mode != GM_Inventory))
                return;

            MWWorld::Ptr object = MWBase::Environment::get().getWorld()->getFacedObject();

            if (winMgr->isConsoleMode())
                winMgr->setConsoleSelectedObject(object);
            else //if ((mode == GM_Container) || (mode == GM_Inventory))
            {
                // pick up object
                if (!object.isEmpty())
                /*
                    Start of tes3mp change (major)

                    Disable unilateral picking up of objects on this client

                    Instead, send an ID_OBJECT_ACTIVATE packet every time an item is made to pick up
                    an item here, and expect the server's reply to our packet to cause the actual
                    picking up of items
                */
                    //winMgr->getInventoryWindow()->pickUpObject(object);
                {
                    mwmp::ObjectList *objectList = mwmp::Main::get().getNetworking()->getObjectList();
                    objectList->reset();
                    objectList->packetOrigin = mwmp::CLIENT_GAMEPLAY;
                    objectList->addObjectActivate(object, MWMechanics::getPlayer());
                    objectList->sendObjectActivate();
                }
                /*
                    End of tes3mp change (major)
                */
            }
        }
    }

    void HUD::onWorldMouseOver(MyGUI::Widget* _sender, int x, int y)
    {
        if (mDragAndDrop->mIsOnDragAndDrop)
        {
            mWorldMouseOver = false;

            MyGUI::IntSize viewSize = MyGUI::RenderManager::getInstance().getViewSize();
            MyGUI::IntPoint cursorPosition = MyGUI::InputManager::getInstance().getMousePosition();
            float mouseX = cursorPosition.left / float(viewSize.width);
            float mouseY = cursorPosition.top / float(viewSize.height);

            MWBase::World* world = MWBase::Environment::get().getWorld();

            // if we can't drop the object at the wanted position, show the "drop on ground" cursor.
            bool canDrop = world->canPlaceObject(mouseX, mouseY);

            if (!canDrop)
                MWBase::Environment::get().getWindowManager()->changePointer("drop_ground");
            else
                MWBase::Environment::get().getWindowManager()->changePointer("arrow");

        }
        else
        {
            MWBase::Environment::get().getWindowManager()->changePointer("arrow");
            mWorldMouseOver = true;
        }
    }

    void HUD::onWorldMouseLostFocus(MyGUI::Widget* _sender, MyGUI::Widget* _new)
    {
        MWBase::Environment::get().getWindowManager()->changePointer("arrow");
        mWorldMouseOver = false;
    }

    void HUD::onHMSClicked(MyGUI::Widget* _sender)
    {
        MWBase::Environment::get().getWindowManager()->toggleVisible(GW_Stats);
    }

    void HUD::onMapClicked(MyGUI::Widget* _sender)
    {
        MWBase::Environment::get().getWindowManager()->toggleVisible(GW_Map);
    }

    void HUD::onWeaponClicked(MyGUI::Widget* _sender)
    {
        const MWWorld::Ptr& player = MWMechanics::getPlayer();
        if (player.getClass().getNpcStats(player).isWerewolf())
        {
            MWBase::Environment::get().getWindowManager()->messageBox("#{sWerewolfRefusal}");
            return;
        }

        MWBase::Environment::get().getWindowManager()->toggleVisible(GW_Inventory);
    }

    void HUD::onMagicClicked(MyGUI::Widget* _sender)
    {
        const MWWorld::Ptr& player = MWMechanics::getPlayer();
        if (player.getClass().getNpcStats(player).isWerewolf())
        {
            MWBase::Environment::get().getWindowManager()->messageBox("#{sWerewolfRefusal}");
            return;
        }

        MWBase::Environment::get().getWindowManager()->toggleVisible(GW_Magic);
    }

    void HUD::setCellName(const std::string& cellName)
    {
        if (mCellName != cellName)
        {
            mCellNameTimer = 5.0f;
            mCellName = cellName;

            mCellNameBox->setCaptionWithReplacing("#{sCell=" + mCellName + "}");
            mCellNameBox->setVisible(mMapVisible);
        }
    }

    void HUD::onFrame(float dt)
    {
        LocalMapBase::onFrame(dt);


        if (mGameTimeBox)
        {
            mGameTimeUpdateTimer -= dt;
            if (mGameTimeUpdateTimer <= 0.f)
            {
                const float gameHour = MWBase::Environment::get().getWorld()->getTimeStamp().getHour();
                int hours = static_cast<int>(std::floor(gameHour)) % 24;
                int minutes = static_cast<int>(std::floor((gameHour - std::floor(gameHour)) * 60.f + 0.5f));
                if (minutes >= 60)
                {
                    minutes = 0;
                    hours = (hours + 1) % 24;
                }

                std::ostringstream stream;
                stream << std::setfill('0') << std::setw(2) << hours << ':'
                       << std::setfill('0') << std::setw(2) << minutes;
                mGameTimeBox->setCaption(stream.str());

                mGameTimeUpdateTimer = 0.2f;
            }
        }

        mCellNameTimer -= dt;
        mWeaponSpellTimer -= dt;
        if (mCellNameTimer < 0)
            mCellNameBox->setVisible(false);
        if (mWeaponSpellTimer < 0)
            mWeaponSpellBox->setVisible(false);

        mFpsAccumulatedTime += dt;
        ++mFpsFrameCount;
        mFpsUpdateTimer -= dt;
        if (mFpsBox && mFpsUpdateTimer <= 0.f)
        {
            const float safeTime = std::max(0.0001f, mFpsAccumulatedTime);
            const int fps = static_cast<int>(std::lround(static_cast<double>(mFpsFrameCount) / safeTime));
            mFpsBox->setCaption(MyGUI::utility::toString(fps));
            mFpsUpdateTimer = 0.25f;
            mFpsAccumulatedTime = 0.f;
            mFpsFrameCount = 0;
        }

        mEnemyHealthTimer -= dt;
        if (mEnemyHealth->getVisible() && mEnemyHealthTimer < 0)
        {
            mEnemyHealth->setVisible(false);
            mWeaponSpellBox->setPosition(mWeaponSpellBox->getPosition() + MyGUI::IntPoint(0,20));
        }

        if (mIsDrowning)
            mDrowningFlashTheta += dt * osg::PI*2;

        mSpellIcons->updateWidgets(mEffectBox, true);

        if (mEnemyActorId != -1 && mEnemyHealth->getVisible())
        {
            updateEnemyHealthBar();
        }

        if (mIsDrowning)
        {
            float intensity = (cos(mDrowningFlashTheta) + 2.0f) / 3.0f;

            mDrowningFlash->setAlpha(intensity);
        }

        // TES3MP modern HUD: first update bar visibility/alpha (autohide), then
        // update icons which read the bar's fresh alpha value.  We set
        // forcedVisible based on drawState BEFORE the autohide pass so the bars
        // get pinned correctly.  The icons are then driven in updateReadyMode.
        {
            MWWorld::Ptr player = MWMechanics::getPlayer();
            if (!player.isEmpty())
            {
                const MWMechanics::CreatureStats& stats =
                    player.getClass().getCreatureStats(player);
                const int ds = static_cast<int>(stats.getDrawState());
                mStaminaBarState.forcedVisible = (ds == MWMechanics::DrawState_Weapon);
                mMagickaBarState.forcedVisible = (ds == MWMechanics::DrawState_Spell);
            }
        }

        updateAutoHideBar(mHealthFrame, mHealthBarState, dt);
        updateAutoHideBar(mMagickaFrame, mMagickaBarState, dt);
        updateAutoHideBar(mFatigueFrame, mStaminaBarState, dt);

        updateReadyMode();

        refreshAllyFrames(dt);
    }


    void HUD::registerBarChange(AutoHideBarState& state, int current, int modified)
    {
        const bool changed = !state.initialized || state.current != current || state.modified != modified;
        state.current = current;
        state.modified = modified;
        state.initialized = true;

        if (changed)
            state.idleTimer = 0.f;

        state.alpha = 1.f;
    }

    void HUD::applyBarAlpha(MyGUI::Widget* widget, float alpha)
    {
        if (!widget)
            return;

        widget->setAlpha(std::max(0.f, std::min(1.f, alpha)));
    }

    void HUD::updateAutoHideBar(MyGUI::Widget* frame, AutoHideBarState& state, float dt)
    {
        if (!frame || !state.initialized)
            return;

        if (!mHmsBaseVisible)
        {
            frame->setVisible(false);
            return;
        }

        // Ready-mode override: keep the bar pinned visible with full alpha.
        if (state.forcedVisible)
        {
            state.idleTimer = 0.f;
            state.alpha = 1.f;
            frame->setVisible(true);
            applyBarAlpha(frame, 1.f);
            return;
        }

        state.idleTimer += dt;

        const bool isFull = state.modified <= 0 || state.current >= state.modified;
        const float hideDelay = isFull ? 7.f : 20.f;
        const float fadeDuration = 0.35f;

        float targetAlpha = 1.f;
        if (state.idleTimer > hideDelay)
            targetAlpha = std::max(0.f, 1.f - (state.idleTimer - hideDelay) / fadeDuration);

        state.alpha = targetAlpha;
        frame->setVisible(state.alpha > 0.f);
        applyBarAlpha(frame, state.alpha);
    }

    // ================================================================
    // TES3MP modern HUD: ready-mode (weapon/spell drawn).
    //   Weapon drawn  -> pin Stamina + Weapon icon visible.
    //   Spell drawn   -> pin Magicka + Spell icon visible.
    //   Nothing drawn -> regular auto-hide.
    // ================================================================
    void HUD::updateReadyMode()
    {
        MWWorld::Ptr player = MWMechanics::getPlayer();
        if (player.isEmpty())
            return;

        const MWMechanics::CreatureStats& stats =
            player.getClass().getCreatureStats(player);
        const int drawState = static_cast<int>(stats.getDrawState());

        const bool weaponDrawn = (drawState == MWMechanics::DrawState_Weapon);
        const bool spellDrawn  = (drawState == MWMechanics::DrawState_Spell);

        // (forcedVisible flags were already set in onFrame() before the
        //  autohide pass ran, so bar alpha values here are fresh.)

        // Icons (WeapBox, SpellBox) are now top-level siblings of the bars in
        // the HUD layout, so their alpha is no longer tied to the bar's alpha
        // via the parent/child chain.  Drive it ourselves:
        //   * when the matching weapon/spell is drawn    -> alpha 1.0
        //   * otherwise, ride the bar's alpha but never below 0.5
        //   * if the base HUD is turned off entirely     -> hidden
        auto driveIcon = [this](MyGUI::Widget* icon, const AutoHideBarState& st, bool drawn, bool baseVisible)
        {
            if (!icon) return;
            if (!baseVisible)
            {
                icon->setVisible(false);
                return;
            }
            icon->setVisible(true);
            float a = drawn ? 1.f : std::max(0.5f, st.alpha);
            applyBarAlpha(icon, a);
        };

        driveIcon(mWeapBox,  mStaminaBarState, weaponDrawn, mHmsBaseVisible && mWeaponVisible);
        driveIcon(mSpellBox, mMagickaBarState, spellDrawn,  mHmsBaseVisible && mSpellVisible);

        // Status bars right under the icons follow the bar's full alpha curve
        // (they lose meaning once the bar is gone, so they can fade with it).
        if (mWeapStatus)  applyBarAlpha(mWeapStatus,  weaponDrawn ? 1.f : mStaminaBarState.alpha);
        if (mSpellStatus) applyBarAlpha(mSpellStatus, spellDrawn  ? 1.f : mMagickaBarState.alpha);

        // Log the transition once for easier debugging.
        if (drawState != mLastDrawState)
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_VERBOSE,
                "HUD ready-mode changed: %d -> %d", mLastDrawState, drawState);
            mLastDrawState = drawState;
        }
    }

    // ================================================================
    // Ally / summon frames.
    //   Collects the player's summon map (SummonKey -> ActorId) plus any
    //   friendly NPC within a small radius that is actively in combat
    //   alongside the player, then pushes their name / level / HP into a
    //   vertical stack of frames on the top-left of the HUD.
    // ================================================================
    void HUD::layoutAllyFrames()
    {
        if (!mAllyFramesBox)
            return;

        const int frameHeight  = 34;
        const int frameSpacing = 4;
        int y = 0;
        for (AllyFrameWidgets& f : mAllyFrames)
        {
            if (!f.root) continue;
            f.root->setPosition(0, y);
            f.root->setSize(mAllyFramesBox->getWidth(), frameHeight);
            y += frameHeight + frameSpacing;
        }
    }

    void HUD::refreshAllyFrames(float dt)
    {
        if (!mAllyFramesBox)
            return;

        // Build the up-to-date list of ally actorIds the HUD should show.
        std::vector<int> wantedIds;

        MWWorld::Ptr player = MWMechanics::getPlayer();
        if (!player.isEmpty())
        {
            // 1. Player's active summons — the summoning subsystem already
            //    maintains a canonical <SummonKey, ActorId> map for us.
            MWMechanics::CreatureStats& playerStats =
                player.getClass().getCreatureStats(player);
            const auto& summonMap = playerStats.getSummonedCreatureMap();
            for (const auto& pair : summonMap)
            {
                if (pair.second > 0)
                    wantedIds.push_back(pair.second);
            }

            // 2. Friendly NPCs currently in combat on the player's side.
            //    TES3MP's MechanicsManager tracks AI packages per actor; the
            //    cleanest way to query "who is fighting with me" without a
            //    radius scan is to iterate the actors already active in the
            //    mechanics manager and pick those whose combat target is
            //    one of my enemies. That code path needs access to
            //    MechanicsManager internals we don't expose here, so this is
            //    left as a TODO hook: when your server-side code already
            //    knows which NPCs are allied (e.g. party members in TES3MP
            //    Arena), push their actorIds into `wantedIds` from that site
            //    and this HUD will pick them up automatically.
        }

        // Remove frames whose actorId is no longer in `wantedIds` OR whose actor
        // is gone from the world.
        for (auto it = mAllyFrames.begin(); it != mAllyFrames.end(); )
        {
            bool keep = false;
            MWWorld::Ptr p = MWBase::Environment::get().getWorld()->searchPtrViaActorId(it->actorId);
            if (!p.isEmpty() && !p.getClass().getCreatureStats(p).isDead())
            {
                for (int id : wantedIds) if (id == it->actorId) { keep = true; break; }
            }
            if (!keep)
            {
                if (it->root)
                    MyGUI::Gui::getInstance().destroyWidget(it->root);
                it = mAllyFrames.erase(it);
            }
            else ++it;
        }

        // Add new frames for newly-appeared allies.
        for (int id : wantedIds)
        {
            bool exists = false;
            for (const AllyFrameWidgets& f : mAllyFrames)
                if (f.actorId == id) { exists = true; break; }
            if (exists) continue;

            AllyFrameWidgets w;
            w.actorId = id;
            w.root = mAllyFramesBox->createWidgetReal<MyGUI::Widget>(
                "HUD_AllyFrame",
                MyGUI::FloatCoord(0.f, 0.f, 1.f, 0.f),
                MyGUI::Align::Top | MyGUI::Align::HStretch,
                "AllyFrame_" + MyGUI::utility::toString(id));
            if (w.root)
            {
                w.root->setAlpha(0.9f);

                w.name = w.root->createWidget<MyGUI::TextBox>(
                    "SandBrightText",
                    MyGUI::IntCoord(6, 2, mAllyFramesBox->getWidth() - 12, 16),
                    MyGUI::Align::HStretch | MyGUI::Align::Top);
                if (w.name)
                {
                    w.name->setFontName("Russo");
                    w.name->setTextShadow(true);
                    w.name->setTextShadowColour(MyGUI::Colour::Black);
                }

                w.health = w.root->createWidget<MyGUI::ProgressBar>(
                    "MW_EnergyBar_Red",
                    MyGUI::IntCoord(6, 20, mAllyFramesBox->getWidth() - 12, 10),
                    MyGUI::Align::HStretch | MyGUI::Align::Top);
                if (w.health)
                {
                    w.health->setProgressRange(100);
                    w.health->setProgressPosition(100);
                }
            }
            mAllyFrames.push_back(w);
        }

        // Update the per-frame text + HP progress.
        for (AllyFrameWidgets& f : mAllyFrames)
        {
            MWWorld::Ptr p = MWBase::Environment::get().getWorld()->searchPtrViaActorId(f.actorId);
            if (p.isEmpty()) continue;

            const std::string name = p.getClass().getName(p);
            const MWMechanics::CreatureStats& st = p.getClass().getCreatureStats(p);
            const int level = st.getLevel();

            if (f.name)
            {
                std::ostringstream os;
                os << name << "  [lvl " << level << "]";
                f.name->setCaption(os.str());
            }

            if (f.health)
            {
                const float cur = st.getHealth().getCurrent();
                const float mx  = std::max(1.f, st.getHealth().getModified());
                const int pct = static_cast<int>(std::max(0.f, std::min(100.f, (cur / mx) * 100.f)));
                f.health->setProgressRange(100);
                f.health->setProgressPosition(pct);
            }
        }

        layoutAllyFrames();
        mAllyFramesBox->setVisible(!mAllyFrames.empty());
    }

    void HUD::setSelectedSpell(const std::string& spellId, int successChancePercent)
    {
        const ESM::Spell* spell =
            MWBase::Environment::get().getWorld()->getStore().get<ESM::Spell>().find(spellId);

        std::string spellName = spell->mName;
        if (spellName != mSpellName && mSpellVisible)
        {
            mWeaponSpellTimer = 5.0f;
            mSpellName = spellName;
            mWeaponSpellBox->setCaption(mSpellName);
            mWeaponSpellBox->setVisible(true);
        }

        mSpellStatus->setProgressRange(100);
        mSpellStatus->setProgressPosition(successChancePercent);

        mSpellBox->setUserString("ToolTipType", "Spell");
        mSpellBox->setUserString("Spell", spellId);

        // use the icon of the first effect
        const ESM::MagicEffect* effect =
            MWBase::Environment::get().getWorld()->getStore().get<ESM::MagicEffect>().find(spell->mEffects.mList.front().mEffectID);

        std::string icon = effect->mIcon;
        int slashPos = icon.rfind('\\');
        icon.insert(slashPos+1, "b_");
        icon = MWBase::Environment::get().getWindowManager()->correctIconPath(icon);

        mSpellImage->setSpellIcon(icon);
    }

    void HUD::setSelectedEnchantItem(const MWWorld::Ptr& item, int chargePercent)
    {
        std::string itemName = item.getClass().getName(item);
        if (itemName != mSpellName && mSpellVisible)
        {
            mWeaponSpellTimer = 5.0f;
            mSpellName = itemName;
            mWeaponSpellBox->setCaption(mSpellName);
            mWeaponSpellBox->setVisible(true);
        }

        mSpellStatus->setProgressRange(100);
        mSpellStatus->setProgressPosition(chargePercent);

        mSpellBox->setUserString("ToolTipType", "ItemPtr");
        mSpellBox->setUserData(MWWorld::Ptr(item));

        mSpellImage->setItem(item);
    }

    void HUD::setSelectedWeapon(const MWWorld::Ptr& item, int durabilityPercent)
    {
        std::string itemName = item.getClass().getName(item);
        if (itemName != mWeaponName && mWeaponVisible)
        {
            mWeaponSpellTimer = 5.0f;
            mWeaponName = itemName;
            mWeaponSpellBox->setCaption(mWeaponName);
            mWeaponSpellBox->setVisible(true);
        }

        mWeapBox->clearUserStrings();
        mWeapBox->setUserString("ToolTipType", "ItemPtr");
        mWeapBox->setUserData(MWWorld::Ptr(item));

        mWeapStatus->setProgressRange(100);
        mWeapStatus->setProgressPosition(durabilityPercent);

        mWeapImage->setItem(item);
    }

    void HUD::unsetSelectedSpell()
    {
        std::string spellName = "#{sNone}";
        if (spellName != mSpellName && mSpellVisible)
        {
            mWeaponSpellTimer = 5.0f;
            mSpellName = spellName;
            mWeaponSpellBox->setCaptionWithReplacing(mSpellName);
            mWeaponSpellBox->setVisible(true);
        }

        mSpellStatus->setProgressRange(100);
        mSpellStatus->setProgressPosition(0);
        mSpellImage->setItem(MWWorld::Ptr());
        mSpellBox->clearUserStrings();
    }

    void HUD::unsetSelectedWeapon()
    {
        std::string itemName = "#{sSkillHandtohand}";
        if (itemName != mWeaponName && mWeaponVisible)
        {
            mWeaponSpellTimer = 5.0f;
            mWeaponName = itemName;
            mWeaponSpellBox->setCaptionWithReplacing(mWeaponName);
            mWeaponSpellBox->setVisible(true);
        }

        mWeapStatus->setProgressRange(100);
        mWeapStatus->setProgressPosition(0);

        MWBase::World *world = MWBase::Environment::get().getWorld();
        MWWorld::Ptr player = world->getPlayerPtr();

        mWeapImage->setItem(MWWorld::Ptr());
        std::string icon = (player.getClass().getNpcStats(player).isWerewolf()) ? "icons\\k\\tx_werewolf_hand.dds" : "icons\\k\\stealth_handtohand.dds";
        mWeapImage->setIcon(icon);

        mWeapBox->clearUserStrings();
        mWeapBox->setUserString("ToolTipType", "Layout");
        mWeapBox->setUserString("ToolTipLayout", "HandToHandToolTip");
        mWeapBox->setUserString("Caption_HandToHandText", itemName);
        mWeapBox->setUserString("ImageTexture_HandToHandImage", icon);
    }

    void HUD::setCrosshairVisible(bool visible)
    {
        mCrosshair->setVisible (visible);
    }

    void HUD::setCrosshairOwned(bool owned)
    {
        if(owned)
        {
            mCrosshair->changeWidgetSkin("HUD_Crosshair_Owned");
        }
        else
        {
            mCrosshair->changeWidgetSkin("HUD_Crosshair");
        }
    }

    void HUD::setHmsVisible(bool visible)
    {
        mHmsBaseVisible = visible;

        mHealth->setVisible(visible);
        mMagicka->setVisible(visible);
        mStamina->setVisible(visible);

        if (!visible)
        {
            mHealthFrame->setVisible(false);
            mMagickaFrame->setVisible(false);
            mFatigueFrame->setVisible(false);
        }
        else
        {
            registerBarChange(mHealthBarState, mHealthBarState.current, mHealthBarState.modified);
            registerBarChange(mMagickaBarState, mMagickaBarState.current, mMagickaBarState.modified);
            registerBarChange(mStaminaBarState, mStaminaBarState.current, mStaminaBarState.modified);

            mHealthFrame->setVisible(true);
            mMagickaFrame->setVisible(true);
            mFatigueFrame->setVisible(true);
            applyBarAlpha(mHealthFrame, 1.f);
            applyBarAlpha(mMagickaFrame, 1.f);
            applyBarAlpha(mFatigueFrame, 1.f);
        }

        updatePositions();
    }

    void HUD::setWeapVisible(bool visible)
    {
        mWeapBox->setVisible(visible);
        updatePositions();
    }

    void HUD::setSpellVisible(bool visible)
    {
        mSpellBox->setVisible(visible);
        updatePositions();
    }

    void HUD::setSneakVisible(bool visible)
    {
        mSneakBox->setVisible(visible);
        updatePositions();
    }

    void HUD::setEffectVisible(bool visible)
    {
        mEffectBox->setVisible (visible);
        updatePositions();
    }

    void HUD::setMinimapVisible(bool visible)
    {
        mMinimapBox->setVisible (visible);
        updatePositions();
    }

    void HUD::updatePositions()
    {
        int weapDx = 0, spellDx = 0, sneakDx = 0;
        if (!mHealth->getVisible())
            sneakDx = spellDx = weapDx = mWeapBoxBaseLeft - mHealthManaStaminaBaseLeft;

        if (!mWeapBox->getVisible())
        {
            spellDx += mSpellBoxBaseLeft - mWeapBoxBaseLeft;
            sneakDx = spellDx;
        }

        if (!mSpellBox->getVisible())
            sneakDx += mSneakBoxBaseLeft - mSpellBoxBaseLeft;

        mWeaponVisible = mWeapBox->getVisible();
        mSpellVisible = mSpellBox->getVisible();
        if (!mWeaponVisible && !mSpellVisible)
            mWeaponSpellBox->setVisible(false);

        mWeapBox->setPosition(mWeapBoxBaseLeft - weapDx, mWeapBox->getTop());
        mSpellBox->setPosition(mSpellBoxBaseLeft - spellDx, mSpellBox->getTop());
        mSneakBox->setPosition(mSneakBoxBaseLeft - sneakDx, mSneakBox->getTop());

        const MyGUI::IntSize& viewSize = MyGUI::RenderManager::getInstance().getViewSize();

        // effect box can have variable width -> variable left coordinate
        int effectsDx = 0;
        if (!mMinimapBox->getVisible ())
            effectsDx = mEffectBoxBaseRight - mMinimapBoxBaseRight;

        mMapVisible = mMinimapBox->getVisible ();
        if (!mMapVisible)
            mCellNameBox->setVisible(false);

        mEffectBox->setPosition((viewSize.width - mEffectBoxBaseRight) - mEffectBox->getWidth() + effectsDx, mEffectBox->getTop());
    }

    void HUD::updateEnemyHealthBar()
    {
        MWWorld::Ptr enemy = MWBase::Environment::get().getWorld()->searchPtrViaActorId(mEnemyActorId);
        if (enemy.isEmpty())
            return;
        MWMechanics::CreatureStats& stats = enemy.getClass().getCreatureStats(enemy);
        mEnemyHealth->setProgressRange(100);
        // Health is usually cast to int before displaying. Actors die whenever they are < 1 health.
        // Therefore any value < 1 should show as an empty health bar. We do the same in statswindow :)
        mEnemyHealth->setProgressPosition(static_cast<size_t>(stats.getHealth().getCurrent() / stats.getHealth().getModified() * 100));

        static const float fNPCHealthBarFade = MWBase::Environment::get().getWorld()->getStore().get<ESM::GameSetting>().find("fNPCHealthBarFade")->mValue.getFloat();
        if (fNPCHealthBarFade > 0.f)
            mEnemyHealth->setAlpha(std::max(0.f, std::min(1.f, mEnemyHealthTimer/fNPCHealthBarFade)));

    }

    void HUD::setEnemy(const MWWorld::Ptr &enemy)
    {
        mEnemyActorId = enemy.getClass().getCreatureStats(enemy).getActorId();
        mEnemyHealthTimer = MWBase::Environment::get().getWorld()->getStore().get<ESM::GameSetting>().find("fNPCHealthBarTime")->mValue.getFloat();
        if (!mEnemyHealth->getVisible())
            mWeaponSpellBox->setPosition(mWeaponSpellBox->getPosition() - MyGUI::IntPoint(0,20));
        mEnemyHealth->setVisible(true);
        updateEnemyHealthBar();
    }

    void HUD::resetEnemy()
    {
        mEnemyActorId = -1;
        mEnemyHealthTimer = -1;
    }

    void HUD::clear()
    {
        unsetSelectedSpell();
        unsetSelectedWeapon();
        resetEnemy();

        // Tear down any ally frames that were created via createWidget().
        for (AllyFrameWidgets& f : mAllyFrames)
        {
            if (f.root)
                MyGUI::Gui::getInstance().destroyWidget(f.root);
        }
        mAllyFrames.clear();
    }

    void HUD::customMarkerCreated(MyGUI::Widget *marker)
    {
        marker->eventMouseButtonClick += MyGUI::newDelegate(this, &HUD::onMapClicked);
    }

    void HUD::doorMarkerCreated(MyGUI::Widget *marker)
    {
        marker->eventMouseButtonClick += MyGUI::newDelegate(this, &HUD::onMapClicked);
    }

}
