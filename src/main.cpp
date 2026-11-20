
#include <Geode/Geode.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>
#include <Geode/modify/LevelCell.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/loader/GameEvent.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/ui/ScrollLayer.hpp>

using namespace geode::prelude;

// ---- Layout constants ----
constexpr float POPUP_WIDTH = 300.0f;
constexpr float POPUP_HEIGHT = 280.0f;
constexpr float INPUT_WIDTH = 240.0f;

constexpr float SCROLL_WIDTH = 280.0f;
constexpr float SCROLL_HEIGHT = 160.0f;
constexpr float CONTENT_HEIGHT = 320.0f;

constexpr size_t MAX_NAME_LENGTH = 50;
constexpr size_t MAX_AUTHOR_LENGTH = 30;
constexpr size_t MAX_DOWNLOADS_LENGTH = 10;
constexpr size_t MAX_LIKES_LENGTH = 10;

constexpr float ICON_MAX_SIZE = 50.0f;
constexpr float ICON_MAX_GAP = 30.0f;
constexpr float MAX_LABEL_WIDTH = 180.0f;

constexpr bool CELL_KEEP_LEFT_EDGE = true;

static bool s_popupOpen = false;

struct LevelOverride {
    std::string name;
    std::string author;
    std::string downloads;
    std::string likes;
    std::string listDownloads;
    std::string listLikes;
};

static int getLevelID(GJGameLevel* level) {
    return level ? level->m_levelID.value() : 0;
}

// ---- Persistence ----

static std::string keyFor(int levelID, char const* field) {
    return fmt::format("level_{}_{}", levelID, field);
}

static LevelOverride loadLevelOverride(int levelID) {
    LevelOverride ov;

    ov.name = Mod::get()->getSavedValue<std::string>(keyFor(levelID, "name"), "");
    ov.author = Mod::get()->getSavedValue<std::string>(keyFor(levelID, "author"), "");
    ov.downloads = Mod::get()->getSavedValue<std::string>(keyFor(levelID, "downloads"), "");
    ov.likes = Mod::get()->getSavedValue<std::string>(keyFor(levelID, "likes"), "");
    ov.listDownloads = Mod::get()->getSavedValue<std::string>(keyFor(levelID, "listDownloads"), "");
    ov.listLikes = Mod::get()->getSavedValue<std::string>(keyFor(levelID, "listLikes"), "");

    return ov;
}

static void saveLevelOverride(int levelID, LevelOverride const& ov) {
    Mod::get()->setSavedValue<std::string>(keyFor(levelID, "name"), ov.name);
    Mod::get()->setSavedValue<std::string>(keyFor(levelID, "author"), ov.author);
    Mod::get()->setSavedValue<std::string>(keyFor(levelID, "downloads"), ov.downloads);
    Mod::get()->setSavedValue<std::string>(keyFor(levelID, "likes"), ov.likes);
    Mod::get()->setSavedValue<std::string>(keyFor(levelID, "listDownloads"), ov.listDownloads);
    Mod::get()->setSavedValue<std::string>(keyFor(levelID, "listLikes"), ov.listLikes);

    log::debug("Saved override for level {}", levelID);
}

static void clearLevelOverride(int levelID) {
    Mod::get()->setSavedValue<std::string>(keyFor(levelID, "name"), "");
    Mod::get()->setSavedValue<std::string>(keyFor(levelID, "author"), "");
    Mod::get()->setSavedValue<std::string>(keyFor(levelID, "downloads"), "");
    Mod::get()->setSavedValue<std::string>(keyFor(levelID, "likes"), "");
    Mod::get()->setSavedValue<std::string>(keyFor(levelID, "listDownloads"), "");
    Mod::get()->setSavedValue<std::string>(keyFor(levelID, "listLikes"), "");

    log::debug("Cleared override for level {}", levelID);
}

// ---- Label helpers ----

static bool setLabelText(CCNode* node, std::string const& text) {
    if (!node) return false;

    if (auto label = typeinfo_cast<CCLabelBMFont*>(node)) {
        label->setString(text.c_str());
        return true;
    }

    if (auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(node)) {
        if (auto label = typeinfo_cast<CCLabelBMFont*>(btn->getNormalImage())) {
            label->setString(text.c_str());
            btn->setContentSize(label->getScaledContentSize());

            auto size = btn->getContentSize();
            label->setPosition(ccp(size.width / 2, size.height / 2));

            return true;
        }
    }

    return false;
}

// ---- Icon helpers ----

static CCRect worldBounds(CCNode* n) {
    auto size = n->getContentSize();
    auto a = n->convertToWorldSpace(ccp(0, 0));
    auto b = n->convertToWorldSpace(ccp(size.width, size.height));

    return CCRect(
        std::min(a.x, b.x),
        std::min(a.y, b.y),
        std::abs(b.x - a.x),
        std::abs(b.y - a.y)
    );
}

struct Neighbor {
    CCNode* node;
    CCPoint worldPos;
    bool onRight;
};

static void collectNeighbors(
    CCNode* root,
    CCNode* nameNode,
    CCRect const& nameBox,
    std::vector<Neighbor>& out,
    bool deep = false
) {
    if (!root) return;

    auto children = root->getChildren();
    if (!children) return;

    float nameCenterY = nameBox.origin.y + nameBox.size.height / 2;
    float nameRight = nameBox.origin.x + nameBox.size.width;
    float nameLeft = nameBox.origin.x;

    for (unsigned int i = 0; i < children->count(); i++) {
        auto child = static_cast<CCNode*>(children->objectAtIndex(i));

        if (!child || child == nameNode || !child->isVisible())
            continue;

        auto box = worldBounds(child);

        if (box.size.width > ICON_MAX_SIZE || box.size.height > ICON_MAX_SIZE) {
            if (deep)
                collectNeighbors(child, nameNode, nameBox, out, true);
            continue;
        }

        if (box.size.width <= 0) continue;

        float centerY = box.origin.y + box.size.height / 2;

        if (std::abs(centerY - nameCenterY) >
            std::max(nameBox.size.height, box.size.height))
            continue;

        float gapRight = box.origin.x - nameRight;
        float gapLeft = nameLeft - (box.origin.x + box.size.width);

        auto parent = child->getParent();
        if (!parent) continue;

        auto worldPos = parent->convertToWorldSpace(child->getPosition());

        if (gapRight >= -5.0f && gapRight <= ICON_MAX_GAP) {
            out.push_back({ child, worldPos, true });
        }
        else if (gapLeft >= -5.0f && gapLeft <= ICON_MAX_GAP) {
            out.push_back({ child, worldPos, false });
        }
    }
}

static void shiftNeighbors(
    std::vector<Neighbor> const& list,
    CCRect const& before,
    CCRect const& after
) {
    float rightDelta =
        (after.origin.x + after.size.width) -
        (before.origin.x + before.size.width);

    float leftDelta = after.origin.x - before.origin.x;

    for (auto const& n : list) {
        auto parent = n.node->getParent();
        if (!parent) continue;

        auto wp = n.worldPos;
        wp.x += n.onRight ? rightDelta : leftDelta;

        n.node->setPosition(parent->convertToNodeSpace(wp));
    }
}

// ---- Apply overrides to level information ----

static void applyOverrides(CCNode* layer, GJGameLevel* level, bool force) {
    if (!layer || !level) return;

    int levelID = getLevelID(level);
    if (levelID <= 0) return;

    auto ov = loadLevelOverride(levelID);

    // Level name
    if (force || !ov.name.empty()) {
        std::string original = level->m_levelName;
        std::string text = ov.name.empty() ? original : ov.name;

        if (!setLabelText(layer->getChildByID("title-label"), text)) {
            log::debug("Could not find title-label");
        }
    }

    // Author
    if (force || !ov.author.empty()) {
        std::string original = level->m_creatorName;
        std::string text = ov.author.empty() ? original : "By " + ov.author;

        auto menu = layer->getChildByID("creator-info-menu");
        auto nameNode = menu ? menu->getChildByID("creator-name") : nullptr;

        if (nameNode) {
            auto before = worldBounds(nameNode);

            std::vector<Neighbor> neighbors;
            collectNeighbors(menu, nameNode, before, neighbors);
            collectNeighbors(layer, nameNode, before, neighbors);

            if (setLabelText(nameNode, text)) {
                menu->updateLayout();
                shiftNeighbors(neighbors, before, worldBounds(nameNode));
            }
        }
    }

    // Downloads
    if (force || !ov.downloads.empty()) {
        std::string original = std::to_string(level->m_downloads);
        std::string text = ov.downloads.empty() ? original : ov.downloads;

        auto label = layer->getChildByID("downloads-label");

        if (auto bm = typeinfo_cast<CCLabelBMFont*>(label)) {
            float oldScale = bm->getScale();
            bm->setString(text.c_str());

            float width = bm->getScaledContentSize().width;

            if (width > 60.0f && width > 0)
                bm->setScale(oldScale * 60.0f / width);
        }
        else if (label) {
            setLabelText(label, text);
        }
    }

    // Likes
    if (force || !ov.likes.empty()) {
        std::string original = std::to_string(level->m_likes);
        std::string text = ov.likes.empty() ? original : ov.likes;

        auto label = layer->getChildByID("likes-label");

        if (auto bm = typeinfo_cast<CCLabelBMFont*>(label)) {
            float oldScale = bm->getScale();
            bm->setString(text.c_str());

            float width = bm->getScaledContentSize().width;

            if (width > 60.0f && width > 0)
                bm->setScale(oldScale * 60.0f / width);
        }
        else if (label) {
            setLabelText(label, text);
        }
    }
}

// ---- List and pause screen helpers ----

static CCLabelBMFont* findLabelWithText(
    CCNode* root,
    std::vector<std::string> const& texts
) {
    if (!root) return nullptr;

    if (auto label = typeinfo_cast<CCLabelBMFont*>(root)) {
        std::string current = label->getString();

        for (auto const& t : texts) {
            if (current == t)
                return label;
        }
    }

    auto children = root->getChildren();
    if (!children) return nullptr;

    for (unsigned int i = 0; i < children->count(); i++) {
        auto child = static_cast<CCNode*>(children->objectAtIndex(i));

        if (auto found = findLabelWithText(child, texts))
            return found;
    }

    return nullptr;
}

static void replaceLabelText(
    CCLabelBMFont* label,
    std::string const& text
) {
    float oldWidth = label->getScaledContentSize().width;

    label->setString(text.c_str());

    float maxWidth = std::max(oldWidth, MAX_LABEL_WIDTH);
    float newWidth = label->getScaledContentSize().width;

    if (newWidth > maxWidth && newWidth > 0) {
        label->setScale(label->getScale() * maxWidth / newWidth);
    }

    if (auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(label->getParent())) {
        btn->setContentSize(label->getScaledContentSize());

        auto size = btn->getContentSize();
        label->setPosition(ccp(size.width / 2, size.height / 2));
    }
}

static std::string formatNumber(int num) {
    std::string s = std::to_string(num);

    int n = static_cast<int>(s.length()) - 3;

    while (n > 0) {
        s.insert(n, ",");
        n -= 3;
    }

    return s;
}

static std::string formatNumberWithSuffix(int num) {
    if (num >= 1000000) {
        double m = num / 1000000.0;
        char buf[32];
        snprintf(buf, sizeof(buf), "%.1fM", m);

        std::string s = buf;

        if (s.find(".0M") != std::string::npos)
            s = s.substr(0, s.length() - 3) + "M";

        return s;
    }
    else if (num >= 1000) {
        double k = num / 1000.0;
        char buf[32];
        snprintf(buf, sizeof(buf), "%.1fK", k);

        std::string s = buf;

        if (s.find(".0K") != std::string::npos)
            s = s.substr(0, s.length() - 3) + "K";

        return s;
    }

    return std::to_string(num);
}

static void keepNameAnchored(
    CCNode* nameNode,
    CCRect const& before,
    std::vector<Neighbor> const& neighbors
) {
    auto after = worldBounds(nameNode);

    float dx;

    if (CELL_KEEP_LEFT_EDGE) {
        dx = before.origin.x - after.origin.x;
    }
    else {
        dx = (before.origin.x + before.size.width / 2) -
             (after.origin.x + after.size.width / 2);
    }

    if (auto parent = nameNode->getParent()) {
        auto wp = parent->convertToWorldSpace(nameNode->getPosition());
        wp.x += dx;
        nameNode->setPosition(parent->convertToNodeSpace(wp));
    }

    CCRect corrected(
        after.origin.x + dx,
        after.origin.y,
        after.size.width,
        after.size.height
    );

    shiftNeighbors(neighbors, before, corrected);
}

static void applyToLabels(CCNode* root, GJGameLevel* level) {
    if (!root || !level) return;

    int levelID = getLevelID(level);
    if (levelID <= 0) return;

    auto ov = loadLevelOverride(levelID);

    if (ov.name.empty() && ov.author.empty() &&
        ov.listDownloads.empty() && ov.listLikes.empty())
        return;

    std::string realName = level->m_levelName;
    std::string realAuthor = level->m_creatorName;
    std::string realDownloads = std::to_string(level->m_downloads);
    std::string realLikes = std::to_string(level->m_likes);

    // Name
    if (!ov.name.empty() && !realName.empty()) {
        if (auto label = findLabelWithText(root, { realName })) {
            replaceLabelText(label, ov.name);
        }
    }

    // Author
    if (!ov.author.empty() && !realAuthor.empty()) {
        if (auto label = findLabelWithText(
            root, { realAuthor, "By " + realAuthor }
        )) {
            bool hasPrefix =
                std::string(label->getString()).rfind("By ", 0) == 0;

            CCNode* nameNode = label;

            if (typeinfo_cast<CCMenuItemSpriteExtra*>(label->getParent()))
                nameNode = label->getParent();

            auto before = worldBounds(nameNode);

            std::vector<Neighbor> neighbors;
            collectNeighbors(root, nameNode, before, neighbors, true);

            replaceLabelText(
                label,
                hasPrefix ? "By " + ov.author : ov.author
            );

            keepNameAnchored(nameNode, before, neighbors);
        }
    }

    // List downloads
    if (!ov.listDownloads.empty() && !realDownloads.empty()) {
        std::string formatted = formatNumber(level->m_downloads);
        std::string suffix = formatNumberWithSuffix(level->m_downloads);

        std::string suffixNoDec = suffix;
        size_t dotPos = suffixNoDec.find('.');

        if (dotPos != std::string::npos) {
            suffixNoDec =
                suffixNoDec.substr(0, dotPos) +
                suffixNoDec.substr(dotPos + 2);
        }

        if (auto label = findLabelWithText(
            root, { realDownloads, formatted, suffix, suffixNoDec }
        )) {
            replaceLabelText(label, ov.listDownloads);
        }
    }

    // List likes
    if (!ov.listLikes.empty() && !realLikes.empty()) {
        std::string formatted = formatNumber(level->m_likes);
        std::string suffix = formatNumberWithSuffix(level->m_likes);

        std::string suffixNoDec = suffix;
        size_t dotPos = suffixNoDec.find('.');

        if (dotPos != std::string::npos) {
            suffixNoDec =
                suffixNoDec.substr(0, dotPos) +
                suffixNoDec.substr(dotPos + 2);
        }

        if (auto label = findLabelWithText(
            root, { realLikes, formatted, suffix, suffixNoDec }
        )) {
            replaceLabelText(label, ov.listLikes);
        }
    }
}

// ============================================================
// EDIT POPUP - COMPACT VERSION
// ============================================================

class EditNamePopup : public Popup {
protected:
    Ref<CCNode> m_layer;
    Ref<GJGameLevel> m_level;

    int m_levelID = 0;

    TextInput* m_nameInput = nullptr;
    TextInput* m_authorInput = nullptr;
    TextInput* m_downloadsInput = nullptr;
    TextInput* m_likesInput = nullptr;
    TextInput* m_listDownloadsInput = nullptr;
    TextInput* m_listLikesInput = nullptr;

    ScrollLayer* m_scrollLayer = nullptr;

    bool init(CCNode* layer, GJGameLevel* level) {
        if (!Popup::init(POPUP_WIDTH, POPUP_HEIGHT))
            return false;

        m_layer = layer;
        m_level = level;
        m_levelID = getLevelID(level);

        this->setTitle("Edit Level Names");

        auto current = loadLevelOverride(m_levelID);

        // Compact scroll area
        m_scrollLayer = ScrollLayer::create({
            0,
            0,
            SCROLL_WIDTH,
            SCROLL_HEIGHT
        });

        m_scrollLayer->m_contentLayer->setContentSize({
            SCROLL_WIDTH,
            CONTENT_HEIGHT
        });

        m_mainLayer->addChildAtPosition(
            m_scrollLayer,
            Anchor::Center,
            ccp(0, 15)
        );

        float yOffset = 295.0f;
        float labelX = 10.0f;
        float inputX = SCROLL_WIDTH / 2.0f;

        // Level name
        auto nameLabel = CCLabelBMFont::create(
            "Level name",
            "goldFont.fnt"
        );

        nameLabel->setScale(0.5f);
        nameLabel->setAnchorPoint(ccp(0.0f, 0.5f));
        nameLabel->setPosition(ccp(labelX, yOffset));
        m_scrollLayer->m_contentLayer->addChild(nameLabel);

        m_nameInput = TextInput::create(
            INPUT_WIDTH,
            std::string(level->m_levelName)
        );

        m_nameInput->setMaxCharCount(MAX_NAME_LENGTH);
        m_nameInput->setString(current.name);
        m_nameInput->setPosition(ccp(inputX, yOffset - 23));
        m_scrollLayer->m_contentLayer->addChild(m_nameInput);

        yOffset -= 50;

        // Author
        auto authorLabel = CCLabelBMFont::create(
            "Author name",
            "goldFont.fnt"
        );

        authorLabel->setScale(0.5f);
        authorLabel->setAnchorPoint(ccp(0.0f, 0.5f));
        authorLabel->setPosition(ccp(labelX, yOffset));
        m_scrollLayer->m_contentLayer->addChild(authorLabel);

        m_authorInput = TextInput::create(
            INPUT_WIDTH,
            std::string(level->m_creatorName)
        );

        m_authorInput->setMaxCharCount(MAX_AUTHOR_LENGTH);
        m_authorInput->setString(current.author);
        m_authorInput->setPosition(ccp(inputX, yOffset - 23));
        m_scrollLayer->m_contentLayer->addChild(m_authorInput);

        yOffset -= 50;

        // Downloads main page
        auto downloadsLabel = CCLabelBMFont::create(
            "Downloads (Main Page)",
            "goldFont.fnt"
        );

        downloadsLabel->setScale(0.5f);
        downloadsLabel->setAnchorPoint(ccp(0.0f, 0.5f));
        downloadsLabel->setPosition(ccp(labelX, yOffset));
        m_scrollLayer->m_contentLayer->addChild(downloadsLabel);

        m_downloadsInput = TextInput::create(INPUT_WIDTH, "");
        m_downloadsInput->setMaxCharCount(MAX_DOWNLOADS_LENGTH);
        m_downloadsInput->setString(current.downloads);
        m_downloadsInput->setPosition(ccp(inputX, yOffset - 23));
        m_scrollLayer->m_contentLayer->addChild(m_downloadsInput);

        yOffset -= 50;

        // Likes main page
        auto likesLabel = CCLabelBMFont::create(
            "Likes (Main Page)",
            "goldFont.fnt"
        );

        likesLabel->setScale(0.5f);
        likesLabel->setAnchorPoint(ccp(0.0f, 0.5f));
        likesLabel->setPosition(ccp(labelX, yOffset));
        m_scrollLayer->m_contentLayer->addChild(likesLabel);

        m_likesInput = TextInput::create(INPUT_WIDTH, "");
        m_likesInput->setMaxCharCount(MAX_LIKES_LENGTH);
        m_likesInput->setString(current.likes);
        m_likesInput->setPosition(ccp(inputX, yOffset - 23));
        m_scrollLayer->m_contentLayer->addChild(m_likesInput);

        yOffset -= 50;

        // Downloads list cells
        auto listDownloadsLabel = CCLabelBMFont::create(
            "Downloads (List Cells)",
            "goldFont.fnt"
        );

        listDownloadsLabel->setScale(0.5f);
        listDownloadsLabel->setAnchorPoint(ccp(0.0f, 0.5f));
        listDownloadsLabel->setPosition(ccp(labelX, yOffset));
        m_scrollLayer->m_contentLayer->addChild(listDownloadsLabel);

        m_listDownloadsInput = TextInput::create(INPUT_WIDTH, "");
        m_listDownloadsInput->setMaxCharCount(MAX_DOWNLOADS_LENGTH);
        m_listDownloadsInput->setString(current.listDownloads);
        m_listDownloadsInput->setPosition(ccp(inputX, yOffset - 23));
        m_scrollLayer->m_contentLayer->addChild(m_listDownloadsInput);

        yOffset -= 50;

        // Likes list cells
        auto listLikesLabel = CCLabelBMFont::create(
            "Likes (List Cells)",
            "goldFont.fnt"
        );

        listLikesLabel->setScale(0.5f);
        listLikesLabel->setAnchorPoint(ccp(0.0f, 0.5f));
        listLikesLabel->setPosition(ccp(labelX, yOffset));
        m_scrollLayer->m_contentLayer->addChild(listLikesLabel);

        m_listLikesInput = TextInput::create(INPUT_WIDTH, "");
        m_listLikesInput->setMaxCharCount(MAX_LIKES_LENGTH);
        m_listLikesInput->setString(current.listLikes);
        m_listLikesInput->setPosition(ccp(inputX, yOffset - 23));
        m_scrollLayer->m_contentLayer->addChild(m_listLikesInput);

        // Buttons remain outside the scroll area
        auto saveBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Save"),
            this,
            menu_selector(EditNamePopup::onSave)
        );

        m_buttonMenu->addChildAtPosition(
            saveBtn,
            Anchor::Bottom,
            ccp(-50, 35)
        );

        auto resetBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Reset"),
            this,
            menu_selector(EditNamePopup::onReset)
        );

        m_buttonMenu->addChildAtPosition(
            resetBtn,
            Anchor::Bottom,
            ccp(50, 35)
        );

        return true;
    }

    void onSave(CCObject*) {
        LevelOverride ov;

        ov.name = m_nameInput->getString();
        ov.author = m_authorInput->getString();
        ov.downloads = m_downloadsInput->getString();
        ov.likes = m_likesInput->getString();
        ov.listDownloads = m_listDownloadsInput->getString();
        ov.listLikes = m_listLikesInput->getString();

        saveLevelOverride(m_levelID, ov);

        applyOverrides(
            m_layer.data(),
            m_level.data(),
            true
        );

        this->keyBackClicked();
    }

    void onReset(CCObject*) {
        clearLevelOverride(m_levelID);

        m_nameInput->setString("");
        m_authorInput->setString("");
        m_downloadsInput->setString("");
        m_likesInput->setString("");
        m_listDownloadsInput->setString("");
        m_listLikesInput->setString("");

        applyOverrides(
            m_layer.data(),
            m_level.data(),
            true
        );
    }

public:
    static EditNamePopup* create(
        CCNode* layer,
        GJGameLevel* level
    ) {
        auto ret = new EditNamePopup();

        if (ret->init(layer, level)) {
            ret->autorelease();
            return ret;
        }

        delete ret;
        return nullptr;
    }

    ~EditNamePopup() {
        s_popupOpen = false;
    }
};

// ---- Level info hook ----

class $modify(FakeNamesLevelInfoLayer, LevelInfoLayer) {
    bool init(GJGameLevel* level, bool challenge) {
        if (!LevelInfoLayer::init(level, challenge))
            return false;

        log::debug(
            "LevelInfoLayer::init for level {}",
            getLevelID(level)
        );

        applyOverrides(this, level, false);

        return true;
    }
};

// ---- Debug labels ----

static void logAllLabels(CCNode* node, int depth = 0) {
    if (!node) return;

    if (auto label = typeinfo_cast<CCLabelBMFont*>(node)) {
        log::debug(
            "Label at depth {}: '{}'",
            depth,
            label->getString()
        );
    }

    auto children = node->getChildren();
    if (!children) return;

    for (unsigned int i = 0; i < children->count(); i++) {
        logAllLabels(
            static_cast<CCNode*>(children->objectAtIndex(i)),
            depth + 1
        );
    }
}

// ---- Level cells hook ----

class $modify(FakeNamesLevelCell, LevelCell) {
    void loadCustomLevelCell() {
        LevelCell::loadCustomLevelCell();

        log::debug(
            "LevelCell::loadCustomLevelCell for level {}",
            getLevelID(m_level)
        );

        logAllLabels(this);
        applyToLabels(this, m_level);
    }
};

// ---- Pause screen hook ----

class $modify(FakeNamesPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        auto playLayer = PlayLayer::get();

        if (playLayer) {
            applyToLabels(this, playLayer->m_level);
        }
    }
};

// ---- Keybind ----

$on_game(Loaded) {
    listenForKeybindSettingPresses(
        "open-popup-keybind",
        [](Keybind const& keybind,
           bool down,
           bool repeat,
           double timestamp) {

            if (!down || repeat) return;
            if (s_popupOpen) return;

            auto scene =
                CCDirector::sharedDirector()->getRunningScene();

            if (!scene) return;

            auto layer =
                scene->getChildByType<LevelInfoLayer>(0);

            if (!layer || !layer->m_level) {
                log::debug(
                    "Keybind pressed but no LevelInfoLayer is open"
                );
                return;
            }

            if (getLevelID(layer->m_level) <= 0) {
                log::debug(
                    "Local levels are not supported"
                );
                return;
            }

            auto popup = EditNamePopup::create(
                layer,
                layer->m_level
            );

            if (popup) {
                s_popupOpen = true;
                popup->show();

                log::debug("Opened compact edit popup");
            }
        }
    );
}
