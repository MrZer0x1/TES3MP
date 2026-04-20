#include "GUIChat.hpp"

#include <MyGUI_EditBox.h>
#include <MyGUI_ScrollView.h>
#include <MyGUI_InputManager.h>
#include <MyGUI_Colour.h>

#include <boost/filesystem.hpp>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

#include "apps/openmw/mwbase/environment.hpp"
#include "apps/openmw/mwgui/windowmanagerimp.hpp"
#include "apps/openmw/mwgui/hud.hpp"
#include "apps/openmw/mwinput/inputmanagerimp.hpp"
#include <components/files/configurationmanager.hpp>
#include <components/openmw-mp/TimedLog.hpp>

#include "../Networking.hpp"
#include "../Main.hpp"
#include "../LocalPlayer.hpp"

#include "../GUIController.hpp"


namespace mwmp
{
    GUIChat::GUIChat(int x, int y, int w, int h)
            : WindowBase("tes3mp_chat.layout")           // fallback layout
            , mCommandLine(nullptr)
            , mHistory(nullptr)
            , mHistoryView(nullptr)
            , mChatPanel(nullptr)
            , windowState(CHAT_DISABLED)
            , editState(false)
            , delay(3.f)
            , curTime(0.f)
            , mUserScrolledUp(false)
            , mScrollOffset(0)
            , mEmbeddedMode(false)
            , mPendingScrollToBottom(false)
            , mHtmlLogOpen(false)
    {
        openHtmlLog();
        // 1. First try to rebind onto widgets that the (modern) HUD layout
        //    has already created for us. If that succeeds we hide the
        //    standalone window entirely and operate in "embedded" mode.
        //    Cast is required because MWBase::WindowManager is the abstract
        //    interface, and getHud() lives on the concrete MWGui subclass.
        //    There is only one concrete implementation (MWGui::WindowManager)
        //    so a static_cast is safe and avoids RTTI dependencies.
        MWGui::HUD* hud = nullptr;
        MWBase::WindowManager* wmBase = MWBase::Environment::get().getWindowManager();
        if (wmBase)
        {
            MWGui::WindowManager* wm = static_cast<MWGui::WindowManager*>(wmBase);
            hud = wm->getHud();
        }
        if (hud)
        {
            mChatPanel   = hud->getChatPanel();
            mHistoryView = dynamic_cast<MyGUI::ScrollView*>(hud->getChatHistoryView());
            mHistory     = hud->getChatHistory();
            mCommandLine = hud->getChatCommand();
        }

        if (mChatPanel && mHistory && mCommandLine)
        {
            mEmbeddedMode = true;

            // The standalone window (loaded from tes3mp_chat.layout) is no
            // longer needed — keep it allocated but invisible so the base
            // class still has its mMainWidget reference.
            setVisible(false);
            setCoord(0, 0, 1, 1);

            // Move chat panel into the position requested by the caller (from
            // the Chat settings section).  If the user already had custom
            // coordinates, they still apply.
            if (w > 0 && h > 0)
                mChatPanel->setCoord(x, y, w, h);
        }
        else
        {
            // Fallback: use the old standalone layout (this is the path that
            // runs when someone boots with an older HUD layout).
            setCoord(x, y, w, h);
            getWidget(mCommandLine, "edit_Command");
            getWidget(mHistory,     "list_History");
            mHistoryView = nullptr;
            mChatPanel   = mMainWidget;
        }

        // 2. Common wiring (works in both modes).
        if (mCommandLine)
        {
            mCommandLine->eventEditSelectAccept += newDelegate(this, &GUIChat::acceptCommand);
            mCommandLine->eventKeyButtonPressed += newDelegate(this, &GUIChat::keyPress);
        }

        setTitle("Chat");

        if (mHistory)
        {
            mHistory->setOverflowToTheLeft(true);
            mHistory->setEditWordWrap(true);
            mHistory->setTextShadow(true);
            mHistory->setTextShadowColour(MyGUI::Colour::Black);
            mHistory->setNeedKeyFocus(false);
        }

        // 3. Mouse-wheel capture. When the chat panel is showing (or hovered)
        //    the wheel should scroll the history instead of cycling the
        //    player's active spells / quickslots. We hook several widgets so
        //    the event is caught no matter where the cursor is over the
        //    panel.
        if (mChatPanel)
            mChatPanel->eventMouseWheel += newDelegate(this, &GUIChat::onMouseWheel);
        if (mHistoryView)
            mHistoryView->eventMouseWheel += newDelegate(this, &GUIChat::onMouseWheel);
        if (mHistory)
            mHistory->eventMouseWheel += newDelegate(this, &GUIChat::onMouseWheel);
        if (mCommandLine)
            mCommandLine->eventMouseWheel += newDelegate(this, &GUIChat::onMouseWheel);

        // 4. Starting state.
        if (mCommandLine)
            mCommandLine->setVisible(false);
        if (mChatPanel)
            mChatPanel->setVisible(false);

        delay = 3.f;
    }

    void GUIChat::onOpen()
    {
        setEditState(false);
        if (windowState == CHAT_DISABLED)
            windowState = CHAT_ENABLED;
    }

    void GUIChat::onClose()
    {
        setEditState(false);
    }

    bool GUIChat::exit()
    {
        return true;
    }

    bool GUIChat::getEditState()
    {
        return editState;
    }

    void GUIChat::acceptCommand(MyGUI::EditBox *_sender)
    {
        if (!mCommandLine) return;
        const std::string &cm = mCommandLine->getOnlyText();

        if (cm.empty())
        {
            mCommandLine->setCaption("");
            setEditState(false);
            return;
        }

        LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Player: %s", cm.c_str());

        if (mCommandHistory.empty() || mCommandHistory.back() != cm)
            mCommandHistory.push_back(cm);
        mCurrent = mCommandHistory.end();
        mEditString.clear();

        mCommandLine->setCaption("");
        setEditState(false);
        send(cm);
    }

    void GUIChat::onResChange(int width, int height)
    {
        if (mEmbeddedMode && mChatPanel)
        {
            // Keep the panel anchored to the lower-left, scale width with viewport.
            int w = std::min(width - 40, std::max(360, width / 3));
            int h = std::max(160, height / 4);
            mChatPanel->setCoord(10, height - h - 10, w, h);
        }
        else
        {
            setCoord(10, 10, width - 10, height / 2);
        }
    }

    void GUIChat::setFont(const std::string &fntName)
    {
        if (mHistory)      mHistory->setFontName(fntName);
        if (mCommandLine)  mCommandLine->setFontName(fntName);
    }

    void GUIChat::print(const std::string &msg, const std::string &color)
    {
        printImpl(msg, color, /*logToHtml=*/true);
    }

    void GUIChat::printImpl(const std::string &msg, const std::string &color, bool logToHtml)
    {
        // In HIDDENMODE we temporarily reveal the panel on new traffic.
        if (windowState == CHAT_HIDDENMODE && mChatPanel && !mChatPanel->getVisible())
        {
            if (mEmbeddedMode)
                mChatPanel->setVisible(true);
            else
                setVisible(true);
            curTime = 0.f;
        }

        if (msg.empty())
        {
            clean();
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Chat cleaned");
            return;
        }

        if (mHistory)
            mHistory->addText(color + msg);
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "%s", msg.c_str());

        // Persistent HTML transcript — only for real player chat, not system
        // ("Command OK" / "Command failed") notifications.
        if (logToHtml)
            writeHtmlMessage(color, msg);

        // Requirement: every new message snaps the chat to the bottom,
        // even if the user was reading old messages.  We also schedule a
        // second pass in the next update() tick because MyGUI lays out
        // the edit-box canvas asynchronously: on the first scroll attempt
        // right after addText(), the canvas height hasn't grown yet.
        mUserScrolledUp = false;
        scrollToBottom();
        mPendingScrollToBottom = true;
    }

    void GUIChat::printOK(const std::string &msg)
    {
        printImpl(msg + "\n", "#FF00FF", /*logToHtml=*/false);
    }

    void GUIChat::printError(const std::string &msg)
    {
        printImpl(msg + "\n", "#FF2222", /*logToHtml=*/false);
    }

    void GUIChat::send(const std::string &str)
    {
        LocalPlayer *localPlayer = Main::get().getLocalPlayer();
        Networking *networking   = Main::get().getNetworking();

        localPlayer->chatMessage = str;

        networking->getPlayerPacket(ID_CHAT_MESSAGE)->setPlayer(localPlayer);
        networking->getPlayerPacket(ID_CHAT_MESSAGE)->Send();
    }

    void GUIChat::clean()
    {
        if (mHistory)
            mHistory->setCaption("");
        mScrollOffset   = 0;
        mUserScrolledUp = false;
        if (mHistoryView)
            mHistoryView->setViewOffset(MyGUI::IntPoint(0, 0));
    }

    void GUIChat::pressedChatMode()
    {
        windowState++;
        if (windowState == 3) windowState = 0;

        std::string chatMode = windowState == CHAT_DISABLED ? "Chat hidden" :
                               windowState == CHAT_ENABLED ? "Chat visible" :
                               "Chat appearing when needed";

        LOG_MESSAGE_SIMPLE(TimedLog::LOG_VERBOSE, "Switch chat mode to %s", chatMode.c_str());
        MWBase::Environment::get().getWindowManager()->messageBox(chatMode);

        auto applyVis = [this](bool v) {
            if (mEmbeddedMode && mChatPanel)
                mChatPanel->setVisible(v);
            else
                setVisible(v);
        };

        switch (windowState)
        {
            case CHAT_DISABLED:
                applyVis(false);
                setEditState(false);
                break;
            case CHAT_ENABLED:
                applyVis(true);
                break;
            default: // CHAT_HIDDENMODE
                applyVis(true);
                curTime = 0.f;
        }
    }

    void GUIChat::setEditState(bool state)
    {
        editState = state;
        if (mCommandLine)
            mCommandLine->setVisible(editState);
        MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(
            editState ? mCommandLine : nullptr);

        // When the user closes the input line the "scrolled-up" latch resets
        // so the next message auto-scrolls back to the bottom.
        if (!state)
            mUserScrolledUp = false;
    }

    void GUIChat::pressedSay()
    {
        if (windowState == CHAT_DISABLED)
            return;

        if (mCommandLine && !mCommandLine->getVisible())
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_VERBOSE, "Opening chat.");

        if (windowState == CHAT_HIDDENMODE)
        {
            if (mEmbeddedMode && mChatPanel)
                mChatPanel->setVisible(true);
            else
                setVisible(true);
            curTime = 0.f;
        }

        setEditState(true);
    }

    // --------------------------------------------------------------------
    // Scrolling helpers.
    // --------------------------------------------------------------------
    void GUIChat::scrollBy(int deltaPx)
    {
        if (mHistoryView)
        {
            // ScrollView path — move the canvas offset directly.
            MyGUI::IntPoint off = mHistoryView->getViewOffset();
            MyGUI::IntSize  canvas = mHistoryView->getCanvasSize();
            MyGUI::IntSize  view   = mHistoryView->getSize();

            int minY = std::min(0, view.height - canvas.height);
            int newY = off.top + deltaPx;
            if (newY > 0)      newY = 0;
            if (newY < minY)   newY = minY;

            mHistoryView->setViewOffset(MyGUI::IntPoint(off.left, newY));
            mScrollOffset = newY;

            mUserScrolledUp = (newY > minY);
        }
        else if (mHistory)
        {
            // Fallback: scroll the EditBox's internal view.
            size_t cur = mHistory->getVScrollPosition();
            int newPos = static_cast<int>(cur) - deltaPx;  // MyGUI inverts
            if (newPos < 0) newPos = 0;
            mHistory->setVScrollPosition(static_cast<size_t>(newPos));
            mUserScrolledUp = (newPos > 0);
        }
    }

    void GUIChat::scrollToBottom()
    {
        if (mHistoryView)
        {
            MyGUI::IntSize canvas = mHistoryView->getCanvasSize();
            MyGUI::IntSize view   = mHistoryView->getSize();
            int minY = std::min(0, view.height - canvas.height);
            mHistoryView->setViewOffset(MyGUI::IntPoint(0, minY));
            mScrollOffset = minY;
        }
        else if (mHistory)
        {
            mHistory->setVScrollPosition(mHistory->getVScrollRange());
        }
    }

    void GUIChat::onMouseWheel(MyGUI::Widget* /*sender*/, int rel)
    {
        // Only capture the wheel while the chat panel is actually showing;
        // otherwise pass through (so the wheel keeps doing its regular job of
        // cycling the player's magic / inventory quickslots).
        const bool panelShowing =
            (mEmbeddedMode && mChatPanel && mChatPanel->getVisible()) ||
            (!mEmbeddedMode && isVisible());
        if (!panelShowing)
            return;

        // MyGUI gives us `rel` in units of 120 per notch (Windows convention).
        // Convert to pixels: one notch ≈ one text line (~20 px).
        const int linePx = 20;
        const int deltaPx = (rel / 120) * linePx;
        scrollBy(deltaPx);
    }

    void GUIChat::keyPress(MyGUI::Widget *_sender, MyGUI::KeyCode key, MyGUI::Char /*_char*/)
    {
        // Page up / Page down scroll the chat even while typing.
        if (key == MyGUI::KeyCode::PageUp)   { scrollBy(+60); return; }
        if (key == MyGUI::KeyCode::PageDown) { scrollBy(-60); return; }

        if (mCommandHistory.empty()) return;

        if (key == MyGUI::KeyCode::ArrowUp)
        {
            if (mCurrent == mCommandHistory.end())
                mEditString = mCommandLine->getOnlyText();

            if (mCurrent != mCommandHistory.begin())
            {
                --mCurrent;
                mCommandLine->setCaption(*mCurrent);
            }
        }
        else if (key == MyGUI::KeyCode::ArrowDown)
        {
            if (mCurrent != mCommandHistory.end())
            {
                ++mCurrent;

                if (mCurrent != mCommandHistory.end())
                    mCommandLine->setCaption(*mCurrent);
                else
                    mCommandLine->setCaption(mEditString);
            }
        }
    }

    void GUIChat::update(float dt)
    {
        // Second pass of scrollToBottom(), scheduled by print(). MyGUI only
        // recomputes the EditBox canvas height one tick after addText(), so
        // the immediate call in print() can undershoot by a few lines.
        if (mPendingScrollToBottom)
        {
            scrollToBottom();
            mPendingScrollToBottom = false;
        }

        if (windowState == CHAT_HIDDENMODE && !editState)
        {
            const bool showing =
                (mEmbeddedMode && mChatPanel && mChatPanel->getVisible()) ||
                (!mEmbeddedMode && isVisible());
            if (showing)
            {
                curTime += dt;
                if (curTime >= delay)
                {
                    setEditState(false);
                    if (mEmbeddedMode && mChatPanel)
                        mChatPanel->setVisible(false);
                    else
                        setVisible(false);
                }
            }
        }
    }

    void GUIChat::setDelay(float newDelay)
    {
        this->delay = newDelay;
    }

    GUIChat::~GUIChat()
    {
        closeHtmlLog();
    }

    // ========================================================================
    // HTML chat transcript.
    //   One file per client install, appended across sessions, living next
    //   to `tes3mp-client.log` in the OpenMW log directory.
    //   Uses a simple <div class="msg">…</div> per line and preserves the
    //   in-game colour runs via inline <span style="color:#rrggbb">.
    // ========================================================================
    void GUIChat::openHtmlLog()
    {
        try
        {
            Files::ConfigurationManager cfgMgr;
            boost::filesystem::path p = cfgMgr.getLogPath() / "tes3mp-chat.html";

            const bool fileExisted = boost::filesystem::exists(p);
            mHtmlLog.open(p.string().c_str(), std::ios::out | std::ios::app);
            if (!mHtmlLog.is_open())
            {
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
                    "GUIChat: cannot open chat HTML log at %s", p.string().c_str());
                return;
            }
            mHtmlLogOpen = true;

            if (!fileExisted)
            {
                // First-time write: drop a lightweight HTML shell.
                mHtmlLog <<
                    "<!doctype html>\n"
                    "<html lang=\"en\"><head><meta charset=\"utf-8\">\n"
                    "<title>TES3MP chat transcript</title>\n"
                    "<style>\n"
                    "  body { background:#111; color:#ddd; font-family:Consolas,monospace;\n"
                    "         font-size:14px; padding:12px; }\n"
                    "  h1   { color:#f0c060; font-size:16px; margin-top:20px; }\n"
                    "  .msg { white-space:pre-wrap; line-height:1.35; }\n"
                    "  .ts  { color:#888; }\n"
                    "  .session { border-top:1px solid #333; margin-top:14px; padding-top:4px; }\n"
                    "</style></head><body>\n";
            }
            else
            {
                // Separate each new session visually.
                mHtmlLog << "\n<div class=\"session\"></div>\n";
            }

            // Session header with the current wall-clock time.
            auto now  = std::chrono::system_clock::now();
            std::time_t tt = std::chrono::system_clock::to_time_t(now);
            std::tm tm{};
#if defined(_WIN32)
            localtime_s(&tm, &tt);
#else
            localtime_r(&tt, &tm);
#endif
            mHtmlLog << "<h1>Session started "
                     << std::put_time(&tm, "%Y-%m-%d %H:%M:%S")
                     << "</h1>\n";
            mHtmlLog.flush();
        }
        catch (const std::exception& e)
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
                "GUIChat: exception opening HTML log: %s", e.what());
            mHtmlLogOpen = false;
        }
    }

    void GUIChat::closeHtmlLog()
    {
        if (mHtmlLogOpen && mHtmlLog.is_open())
        {
            mHtmlLog << "<!-- session closed -->\n";
            mHtmlLog.flush();
            mHtmlLog.close();
        }
        mHtmlLogOpen = false;
    }

    std::string GUIChat::escapeHtml(const std::string& s)
    {
        std::string out;
        out.reserve(s.size());
        for (char c : s)
        {
            switch (c)
            {
                case '&':  out += "&amp;";  break;
                case '<':  out += "&lt;";   break;
                case '>':  out += "&gt;";   break;
                case '"':  out += "&quot;"; break;
                case '\n': out += "<br>";   break;
                case '\r':                  break;
                default:
                    out += c;
            }
        }
        return out;
    }

    // Converts MyGUI inline colour codes (#RRGGBB and #{RRGGBB}) into HTML
    // <span style="color:#rrggbb">…</span> runs. Non-colour #{tokens} are
    // stripped (they would be e.g. localisation keys that have no meaning
    // outside MyGUI).
    std::string GUIChat::myguiColourToHtml(const std::string& s)
    {
        // Two-step: first, escape HTML on chunks of plain text, then insert
        // <span> open/close tags around colour runs.
        std::string out;
        out.reserve(s.size() + 32);

        bool spanOpen = false;
        size_t i = 0;
        auto closeSpan = [&]() {
            if (spanOpen) { out += "</span>"; spanOpen = false; }
        };

        auto isHex = [](char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        };

        while (i < s.size())
        {
            char c = s[i];
            if (c == '#')
            {
                // Case 1: #{RRGGBB} or #{other-token}
                if (i + 1 < s.size() && s[i + 1] == '{')
                {
                    size_t end = s.find('}', i + 2);
                    if (end != std::string::npos)
                    {
                        std::string inside = s.substr(i + 2, end - (i + 2));
                        if (inside.size() == 6 &&
                            std::all_of(inside.begin(), inside.end(), isHex))
                        {
                            closeSpan();
                            out += "<span style=\"color:#" + inside + "\">";
                            spanOpen = true;
                        }
                        // Otherwise: a localisation/format token — just skip.
                        i = end + 1;
                        continue;
                    }
                }
                // Case 2: plain #RRGGBB
                if (i + 6 < s.size() &&
                    isHex(s[i + 1]) && isHex(s[i + 2]) && isHex(s[i + 3]) &&
                    isHex(s[i + 4]) && isHex(s[i + 5]) && isHex(s[i + 6]))
                {
                    closeSpan();
                    out += "<span style=\"color:#";
                    out.append(s, i + 1, 6);
                    out += "\">";
                    spanOpen = true;
                    i += 7;
                    continue;
                }
            }
            // Regular character — push through HTML escape.
            switch (c)
            {
                case '&':  out += "&amp;";  break;
                case '<':  out += "&lt;";   break;
                case '>':  out += "&gt;";   break;
                case '"':  out += "&quot;"; break;
                case '\n': out += "<br>";   break;
                case '\r':                  break;
                default:
                    out += c;
            }
            ++i;
        }
        closeSpan();
        return out;
    }

    void GUIChat::writeHtmlMessage(const std::string& color, const std::string& msg)
    {
        if (!mHtmlLogOpen || !mHtmlLog.is_open())
            return;

        // Timestamp.
        auto now  = std::chrono::system_clock::now();
        std::time_t tt = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
#if defined(_WIN32)
        localtime_s(&tm, &tt);
#else
        localtime_r(&tt, &tm);
#endif
        std::ostringstream stamp;
        stamp << std::put_time(&tm, "%H:%M:%S");

        // The leading colour is the default span for the line. We still
        // honour any inline colour runs embedded in `msg` itself.
        std::string outer = color;
        if (outer.empty() || outer[0] != '#')
            outer = "#FFFFFF";
        // Trim to #RRGGBB form (MyGUI accepts both # and #{…}; we only care
        // about the hex).
        std::string hex = outer.substr(1);
        if (hex.size() > 6) hex = hex.substr(0, 6);

        mHtmlLog << "<div class=\"msg\"><span class=\"ts\">["
                 << stamp.str() << "]</span> "
                 << "<span style=\"color:#" << hex << "\">"
                 << myguiColourToHtml(msg)
                 << "</span></div>\n";
        mHtmlLog.flush();
    }
}
