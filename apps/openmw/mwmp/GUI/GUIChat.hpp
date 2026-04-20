#ifndef OPENMW_GUICHAT_HPP
#define OPENMW_GUICHAT_HPP

#include <fstream>
#include <list>
#include <string>
#include <vector>

#include <MyGUI_Types.h>
#include <MyGUI_KeyCode.h>

#include "apps/openmw/mwgui/windowbase.hpp"

namespace MWGui { class HUD; }

namespace mwmp
{
    class GUIController;

    /// GUIChat is now a thin controller that binds onto widgets embedded in
    /// the HUD layout (`ChatPanel` / `ChatHistoryView` / `ChatHistory` /
    /// `ChatCommand`).  If those widgets are missing (e.g. someone is running
    /// the pre-modernisation HUD) we fall back to the standalone layout
    /// `tes3mp_chat.layout` so the rest of TES3MP keeps working.
    ///
    /// A parallel HTML transcript of every chat message is written to
    /// `<log-dir>/tes3mp-chat.html` so users can re-read past sessions with
    /// colours preserved exactly as they were shown in-game.
    class GUIChat : public MWGui::WindowBase
    {
        friend class GUIController;
    public:
        enum
        {
            CHAT_DISABLED = 0,
            CHAT_ENABLED,
            CHAT_HIDDENMODE
        } CHAT_WIN_STATE;

        MyGUI::EditBox*    mCommandLine;
        MyGUI::EditBox*    mHistory;        // still an EditBox (multi-line, read-only)
        MyGUI::ScrollView* mHistoryView;    // nullptr in fallback mode
        MyGUI::Widget*     mChatPanel;      // nullptr in fallback mode

        typedef std::list<std::string> StringList;

        StringList mCommandHistory;
        StringList::iterator mCurrent;
        std::string mEditString;

        GUIChat(int x, int y, int w, int h);
        ~GUIChat();

        void pressedChatMode();
        void pressedSay();
        void setDelay(float newDelay);

        void update(float dt);

        virtual void onOpen();
        virtual void onClose();

        virtual bool exit();

        bool getEditState();

        void setFont(const std::string &fntName);

        void onResChange(int width, int height);

        void print(const std::string &msg, const std::string& color = "#FFFFFF");

        void clean();

        void printOK(const std::string &msg);
        void printError(const std::string &msg);

        void send(const std::string &str);

    protected:

    private:
        /// Shared implementation of print/printOK/printError.
        /// logToHtml=false suppresses writing to the persistent chat.html
        /// transcript — used for system notifications so the log stays
        /// a clean record of player conversation only.
        void printImpl(const std::string &msg, const std::string& color, bool logToHtml);
        void keyPress(MyGUI::Widget* _sender,
                      MyGUI::KeyCode key,
                      MyGUI::Char _char);

        void acceptCommand(MyGUI::EditBox* _sender);

        void setEditState(bool state);

        // Scroll wheel handler — bound to both the panel and the history widget
        // so the wheel is captured as long as the chat panel is open.
        void onMouseWheel(MyGUI::Widget* _sender, int _rel);

        // Keeps the scroll offset clamped and (optionally) pinned to the bottom.
        void scrollBy(int deltaPx);
        void scrollToBottom();

        // HTML transcript.
        void openHtmlLog();
        void writeHtmlMessage(const std::string& color, const std::string& msg);
        void closeHtmlLog();
        static std::string escapeHtml(const std::string& s);
        /// Convert MyGUI inline colour markup (#RRGGBB / #{RRGGBB}) into
        /// <span style="color:…"> runs, plus strip #{...} token calls that
        /// aren't plain colours.
        static std::string myguiColourToHtml(const std::string& s);

        int  windowState;
        bool editState;
        float delay;
        float curTime;

        // true when the user has scrolled up manually; in that case new
        // incoming messages should NOT auto-scroll to the bottom.
        bool mUserScrolledUp;

        // Cached scroll state (pixels) for the embedded-HUD ScrollView.
        int  mScrollOffset;

        bool mEmbeddedMode;

        // Pending auto-scroll: set to true after addText() so the next
        // update() tick can re-run scrollToBottom() once MyGUI has laid
        // the canvas out.
        bool mPendingScrollToBottom;

        std::ofstream mHtmlLog;
        bool          mHtmlLogOpen;
    };
}
#endif //OPENMW_GUICHAT_HPP
