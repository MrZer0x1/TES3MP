#ifndef OPENMW_GAME_MWGUI_HUD_H
#define OPENMW_GAME_MWGUI_HUD_H

#include <map>
#include <vector>

#include "mapwindow.hpp"
#include "statswatcher.hpp"

namespace MWWorld
{
    class Ptr;
}

namespace MWGui
{
    class DragAndDrop;
    class SpellIcons;
    class ItemWidget;
    class SpellWidget;

    class HUD : public WindowBase, public LocalMapBase, public StatsListener
    {
    public:
        HUD(CustomMarkerCollection& customMarkers, DragAndDrop* dragAndDrop, MWRender::LocalMap* localMapRender);
        virtual ~HUD();
        void setValue (const std::string& id, const MWMechanics::DynamicStat<float>& value) override;

        /// Set time left for the player to start drowning
        /// @param time time left to start drowning
        /// @param maxTime how long we can be underwater (in total) until drowning starts
        void setDrowningTimeLeft(float time, float maxTime);
        void setDrowningBarVisible(bool visible);

        void setHmsVisible(bool visible);
        void setWeapVisible(bool visible);
        void setSpellVisible(bool visible);
        void setSneakVisible(bool visible);

        void setEffectVisible(bool visible);
        void setMinimapVisible(bool visible);

        void setSelectedSpell(const std::string& spellId, int successChancePercent);
        void setSelectedEnchantItem(const MWWorld::Ptr& item, int chargePercent);
        const MWWorld::Ptr& getSelectedEnchantItem();
        void setSelectedWeapon(const MWWorld::Ptr& item, int durabilityPercent);
        void unsetSelectedSpell();
        void unsetSelectedWeapon();

        void setCrosshairVisible(bool visible);
        void setCrosshairOwned(bool owned);

        void onFrame(float dt) override;

        void setCellName(const std::string& cellName);

        bool getWorldMouseOver() { return mWorldMouseOver; }

        MyGUI::Widget* getEffectBox() { return mEffectBox; }

        // --- TES3MP modern HUD additions ----------------------------------

        /// Returns the embedded chat-panel widget (populated by the HUD layout).
        /// GUIChat attaches its own handlers to the child widgets.
        MyGUI::Widget*  getChatPanel()      { return mChatPanel; }
        MyGUI::Widget*  getChatHistoryView(){ return mChatHistoryView; }
        MyGUI::EditBox* getChatHistory()    { return mChatHistory; }
        MyGUI::EditBox* getChatCommand()    { return mChatCommand; }

        /// Returns the container into which AllyFrames are stacked vertically.
        MyGUI::Widget* getAllyFramesBox()   { return mAllyFramesBox; }

        // ------------------------------------------------------------------

        void setEnemy(const MWWorld::Ptr& enemy);
        void resetEnemy();

        void clear() override;

    private:
        MyGUI::ProgressBar *mHealth, *mMagicka, *mStamina, *mEnemyHealth, *mDrowning;
        MyGUI::TextBox *mHealthText, *mMagickaText, *mStaminaText, *mFpsBox;
        MyGUI::Widget *mHealthFrame, *mMagickaFrame, *mFatigueFrame;
        MyGUI::Widget *mWeapBox, *mSpellBox, *mSneakBox;
        ItemWidget *mWeapImage;
        SpellWidget *mSpellImage;
        MyGUI::ProgressBar *mWeapStatus, *mSpellStatus;
        MyGUI::Widget *mEffectBox, *mMinimapBox;
        MyGUI::Button* mMinimapButton;
        MyGUI::ScrollView* mMinimap;
        MyGUI::ImageBox* mCrosshair;
        MyGUI::TextBox* mCellNameBox;
        MyGUI::TextBox* mWeaponSpellBox;
        MyGUI::TextBox* mGameTimeBox;
        MyGUI::Widget *mDrowningFrame, *mDrowningFlash;

        // --- TES3MP modern HUD members ------------------------------------
        MyGUI::Widget*     mChatPanel;
        MyGUI::Widget*     mChatHistoryView;  // actually a ScrollView at runtime (see GUIChat.cpp)
        MyGUI::EditBox*    mChatHistory;
        MyGUI::EditBox*    mChatCommand;

        MyGUI::Widget*     mAllyFramesBox;

        struct AllyFrameWidgets
        {
            MyGUI::Widget*      root   = nullptr;
            MyGUI::TextBox*     name   = nullptr;
            MyGUI::ProgressBar* health = nullptr;
            int                 actorId = -1;
            float               idleTimer = 0.f;
        };
        std::vector<AllyFrameWidgets> mAllyFrames;

        void layoutAllyFrames();
        void refreshAllyFrames(float dt);
        // ------------------------------------------------------------------

        // bottom left elements
        int mHealthManaStaminaBaseLeft, mWeapBoxBaseLeft, mSpellBoxBaseLeft, mSneakBoxBaseLeft;
        // bottom right elements
        int mMinimapBoxBaseRight, mEffectBoxBaseRight;

        DragAndDrop* mDragAndDrop;

        std::string mCellName;
        float mCellNameTimer;

        std::string mWeaponName;
        std::string mSpellName;
        float mWeaponSpellTimer;
        float mGameTimeUpdateTimer;

        bool mMapVisible;
        bool mWeaponVisible;
        bool mSpellVisible;

        bool mWorldMouseOver;

        SpellIcons* mSpellIcons;

        int mEnemyActorId;
        float mEnemyHealthTimer;

        float mFpsUpdateTimer;
        float mFpsAccumulatedTime;
        int mFpsFrameCount;

        bool  mIsDrowning;
        float mDrowningFlashTheta;


        struct AutoHideBarState
        {
            int current = 0;
            int modified = 0;
            float idleTimer = 0.f;
            float alpha = 1.f;
            bool initialized = false;
            bool forcedVisible = false;  // overridden by ready-mode (weapon/spell drawn)
        };

        AutoHideBarState mHealthBarState;
        AutoHideBarState mMagickaBarState;
        AutoHideBarState mStaminaBarState;
        bool mHmsBaseVisible;

        // Ready-mode (tracks the player's draw-state). When the player readies a
        // weapon we pin the stamina bar + weapon icon visible with full alpha;
        // when they ready a spell/enchanted item we pin magicka + spell icon.
        int mLastDrawState;
        void updateReadyMode();

        void registerBarChange(AutoHideBarState& state, int current, int modified);
        void updateAutoHideBar(MyGUI::Widget* frame, AutoHideBarState& state, float dt);
        void applyBarAlpha(MyGUI::Widget* widget, float alpha);

        void onWorldClicked(MyGUI::Widget* _sender);
        void onWorldMouseOver(MyGUI::Widget* _sender, int x, int y);
        void onWorldMouseLostFocus(MyGUI::Widget* _sender, MyGUI::Widget* _new);
        void onHMSClicked(MyGUI::Widget* _sender);
        void onWeaponClicked(MyGUI::Widget* _sender);
        void onMagicClicked(MyGUI::Widget* _sender);
        void onMapClicked(MyGUI::Widget* _sender);

        // LocalMapBase
        void customMarkerCreated(MyGUI::Widget* marker) override;
        void doorMarkerCreated(MyGUI::Widget* marker) override;

        void updateEnemyHealthBar();

        void updatePositions();
    };
}

#endif
