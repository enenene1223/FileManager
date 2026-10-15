#include "FileSystem.h"
#include "operation.h"

#include <ctime>
#include <iomanip>
#include <sstream>
#include <cctype>
#include "PrivacyCrypto.h"


// 后缀验证辅助函数
static bool is_valid_txt_filename(const std::string& name)
{
    const std::string suffix = ".txt";

    if (name.size() <= suffix.size())
        return false;

    return name.compare(
               name.size() - suffix.size(),
               suffix.size(),
               suffix
               ) == 0;
}

namespace {
bool contains_forbidden_name_character(const std::string &name)
{
    return name.find('/') != std::string::npos || name.find('\\') != std::string::npos
           || name.find('|') != std::string::npos || name.find('\n') != std::string::npos
           || name.find('\r') != std::string::npos;
}
} // namespace

FileSystem::FileSystem()
    : root_(new FileNode("root", true))
    , current_directory_(nullptr)
    , recycle_head_(nullptr)
    , recycle_count_(0)
    , last_error_(FileSystemError::NONE)
    , next_pin_order_(0)
{
    const std::string current_time = now_string();
    root_->created_time_ = current_time;
    root_->modified_time_ = current_time;
    current_directory_ = root_;
}

FileSystem::~FileSystem()
{
    clear_recycle_bin();
    destroy_subtree(root_);
    root_ = nullptr;
    current_directory_ = nullptr;
}


FileNode *FileSystem::create_file(FileNode *parent,
                                  const std::string &name,
                                  const std::string &content)
{
    set_error(FileSystemError::NONE);

    if (parent == nullptr || !owns_active_node(parent)) {
        set_error(FileSystemError::INVALID_TARGET);
        return nullptr;
    }

    if (!parent->is_directory_) {
        set_error(FileSystemError::NOT_DIRECTORY);
        return nullptr;
    }

    if (!is_valid_name(name)) {
        set_error(FileSystemError::INVALID_NAME);
        return nullptr;
    }

    // 只允许创建 .txt 文件
    if (!is_valid_txt_filename(name)) {
        set_error(FileSystemError::INVALID_NAME);
        return nullptr;
    }

    if (has_name_conflict(parent, name)) {
        set_error(FileSystemError::NAME_CONFLICT);
        return nullptr;
    }

    FileNode *node = new FileNode(name, false);
    const std::string current_time = now_string();
    node->created_time_ = current_time;
    node->modified_time_ = current_time;
    node->content_ = content;
    node->size_ = static_cast<long long>(content.size());

    append_child(parent, node);
    return node;
}


FileNode *FileSystem::create_folder(FileNode *parent, const std::string &name)
{
    set_error(FileSystemError::NONE);

    if (parent == nullptr || !owns_active_node(parent)) {
        set_error(FileSystemError::INVALID_TARGET);
        return nullptr;
    }

    if (!parent->is_directory_) {
        set_error(FileSystemError::NOT_DIRECTORY);
        return nullptr;
    }

    if (!is_valid_name(name)) {
        set_error(FileSystemError::INVALID_NAME);
        return nullptr;
    }

    if (has_name_conflict(parent, name)) {
        set_error(FileSystemError::NAME_CONFLICT);
        return nullptr;
    }

    FileNode *node = new FileNode(name, true);
    const std::string current_time = now_string();
    node->created_time_ = current_time;
    node->modified_time_ = current_time;

    append_child(parent, node);
    return node;
}


bool FileSystem::delete_node(FileNode *node)
{
    set_error(FileSystemError::NONE);

    if (node == nullptr || !owns_active_node(node)) {
        set_error(FileSystemError::INVALID_NODE);
        return false;
    }

    if (node == root_) {
        set_error(FileSystemError::ROOT_OPERATION);
        return false;
    }

    const std::string original_path = get_path(node);
    FileNode *old_parent = node->parent_;

    // 记录该节点是否受到上级文件夹的隐私保护。
    // 删除后父指针会断开，因此必须提前判断。
    bool inheritedPrivacy = false;

    for (FileNode* parent = old_parent;
         parent != nullptr;
         parent = parent->parent_)
    {
        if (parent->is_locked_)
        {
            inheritedPrivacy = true;
            break;
        }
    }

    if (!detach_node(node)) {
        set_error(FileSystemError::INVALID_NODE);
        return false;
    }

    // 如果当前目录位于被删除子树中，退回到删除前的父目录。
    if (current_directory_ == node || is_descendant(current_directory_, node)) {
        current_directory_ = old_parent != nullptr ? old_parent : root_;
    }

    node->parent_ = nullptr;
    node->next_sibling_ = nullptr;

    recycle_head_ = new RecycleEntry(
        node,
        original_path,
        recycle_head_,
        inheritedPrivacy
        );
    ++recycle_count_;
    return true;
}


bool FileSystem::restore_node(FileNode *node, FileNode *target_parent)
{
    set_error(FileSystemError::NONE);

    RecycleEntry *entry = find_recycle_entry(node);
    if (entry == nullptr) {
        set_error(FileSystemError::NOT_FOUND);
        return false;
    }

    if (target_parent == nullptr || !owns_active_node(target_parent)) {
        set_error(FileSystemError::INVALID_TARGET);
        return false;
    }

    if (!target_parent->is_directory_) {
        set_error(FileSystemError::NOT_DIRECTORY);
        return false;
    }

    if (has_name_conflict(target_parent, node->name_)) {
        set_error(FileSystemError::NAME_CONFLICT);
        return false;
    }

    // 恢复时如果原先继承的隐私父目录已经不再锁定，
    // 则把保护标志转移到当前回收站根节点，防止内容意外公开。
    if (entry->inherited_privacy)
    {
        bool protectedTarget = false;
        for (FileNode* p = target_parent; p; p = p->parent_)
        {
            if (p->is_locked_) { protectedTarget = true; break; }
        }
        if (!protectedTarget) node->is_locked_ = true;
    }

    node->parent_ = nullptr;
    node->next_sibling_ = nullptr;
    append_child(target_parent, node);

    remove_recycle_entry(entry);
    return true;
}


bool FileSystem::permanent_delete(FileNode *node)
{
    set_error(FileSystemError::NONE);

    RecycleEntry *entry = find_recycle_entry(node);
    if (entry == nullptr) {
        if (node != nullptr && owns_active_node(node)) {
            set_error(FileSystemError::NOT_IN_RECYCLE_BIN);
        } else {
            set_error(FileSystemError::NOT_FOUND);
        }
        return false;
    }

    remove_recycle_entry(entry);
    destroy_subtree(node);
    return true;
}


void FileSystem::clear_recycle_bin()
{
    RecycleEntry *entry = recycle_head_;
    recycle_head_ = nullptr;
    recycle_count_ = 0;

    while (entry != nullptr) {
        RecycleEntry *next = entry->next;
        destroy_subtree(entry->node);
        delete entry;
        entry = next;
    }
}


bool FileSystem::rename_node(FileNode *node, const std::string &new_name)
{
    set_error(FileSystemError::NONE);

    if (node == nullptr || !owns_active_node(node)) {
        set_error(FileSystemError::INVALID_NODE);
        return false;
    }

    if (node == root_) {
        set_error(FileSystemError::ROOT_OPERATION);
        return false;
    }

    if (!is_valid_name(new_name)) {
        set_error(FileSystemError::INVALID_NAME);
        return false;
    }

    // 文件重命名必须保留 .txt 后缀
    if (!node->is_directory_ &&
        !is_valid_txt_filename(new_name))
    {
        set_error(FileSystemError::INVALID_NAME);
        return false;
    }

    if (has_name_conflict(node->parent_, new_name, node)) {
        set_error(FileSystemError::NAME_CONFLICT);
        return false;
    }

    node->name_ = new_name;
    node->modified_time_ = now_string();
    return true;
}


bool FileSystem::move_node(FileNode *node, FileNode *target_parent)
{
    set_error(FileSystemError::NONE);

    if (!can_move(node, target_parent)) {
        if (node == nullptr || !owns_active_node(node)) {
            set_error(FileSystemError::INVALID_NODE);
        } else if (target_parent == nullptr || !owns_active_node(target_parent)) {
            set_error(FileSystemError::INVALID_TARGET);
        } else if (!target_parent->is_directory_) {
            set_error(FileSystemError::NOT_DIRECTORY);
        } else if (node == root_ || node == target_parent || is_descendant(target_parent, node)) {
            set_error(FileSystemError::INVALID_MOVE);
        } else if (has_name_conflict(target_parent, node->name_, node)) {
            set_error(FileSystemError::NAME_CONFLICT);
        } else {
            set_error(FileSystemError::INVALID_MOVE);
        }
        return false;
    }

    // 已经在目标目录，无需操作。
    if (node->parent_ == target_parent) {
        return true;
    }

    if (!detach_node(node)) {
        set_error(FileSystemError::INVALID_NODE);
        return false;
    }

    append_child(target_parent, node);
    node->modified_time_ = now_string();
    return true;
}


FileNode *FileSystem::copy_node(FileNode *node, FileNode *target_parent)
{
    set_error(FileSystemError::NONE);

    if (node == nullptr || !owns_active_node(node)) {
        set_error(FileSystemError::INVALID_NODE);
        return nullptr;
    }

    if (target_parent == nullptr || !owns_active_node(target_parent)) {
        set_error(FileSystemError::INVALID_TARGET);
        return nullptr;
    }

    if (!target_parent->is_directory_) {
        set_error(FileSystemError::NOT_DIRECTORY);
        return nullptr;
    }

    if (node == target_parent || is_descendant(target_parent, node)) {
        set_error(FileSystemError::INVALID_MOVE);
        return nullptr;
    }

    if (has_name_conflict(target_parent, node->name_)) {
        set_error(FileSystemError::NAME_CONFLICT);
        return nullptr;
    }

    return clone_subtree(node, target_parent);
}


FileNode *FileSystem::find_child(FileNode *parent, const std::string &name) const
{
    if (parent == nullptr || !owns_active_node(parent) || !parent->is_directory_) {
        return nullptr;
    }

    FileNode *current = parent->first_child_;
    while (current != nullptr) {
        if (current->name_ == name) {
            return current;
        }
        current = current->next_sibling_;
    }

    return nullptr;
}


FileNode *FileSystem::find_by_path(const std::string &path) const
{
    if (path.empty()) {
        return nullptr;
    }

    const bool absolute = path.front() == '/';
    FileNode *current = absolute ? root_ : current_directory_;

    if (current == nullptr) {
        return nullptr;
    }

    if (path == "/") {
        return root_;
    }

    if (path == ".") {
        return current;
    }

    std::size_t start = absolute ? 1 : 0;

    while (start <= path.size()) {
        const std::size_t end = path.find('/', start);
        const std::size_t actual_end = (end == std::string::npos) ? path.size() : end;
        const std::string part = path.substr(start, actual_end - start);

        if (!part.empty() && part != ".") {
            if (part == "..") {
                if (current->parent_ != nullptr) {
                    current = current->parent_;
                }
            } else {
                current = find_child(current, part);
                if (current == nullptr) {
                    return nullptr;
                }
            }
        }

        if (actual_end == path.size()) {
            break;
        }

        start = actual_end + 1;
    }

    return current;
}


FileNode *FileSystem::get_current_directory() const
{
    return current_directory_;
}


bool FileSystem::change_directory(FileNode *directory)
{
    if (directory == nullptr || !owns_active_node(directory)) {
        return false;
    }

    if (!directory->is_directory_) {
        return false;
    }

    current_directory_ = directory;
    return true;
}


bool FileSystem::change_directory_by_path(const std::string &path)
{
    FileNode *directory = find_by_path(path);
    return directory != nullptr && change_directory(directory);
}


bool FileSystem::set_content(FileNode *file, const std::string &content)
{
    set_error(FileSystemError::NONE);

    if (file == nullptr || !owns_active_node(file)) {
        set_error(FileSystemError::INVALID_NODE);
        return false;
    }

    if (file->is_directory_) {
        set_error(FileSystemError::NOT_FILE);
        return false;
    }

    file->content_ = content;
    file->size_ = static_cast<long long>(content.size());
    file->modified_time_ = now_string();
    return true;
}


const std::string &FileSystem::get_content(FileNode *file) const
{
    static const std::string empty_content;

    if (file == nullptr || !owns_active_node(file) || file->is_directory_) {
        return empty_content;
    }

    return file->content_;
}


// 供受信任的加载器和已验证密码的 GUI 调用：在临时存档、
// 正常目录树与回收站节点之间保留 E1 密文，不改变元数据。
bool FileSystem::set_sealed_content(FileNode* node, const std::string& encoded)
{
    if (!node || node->is_directory_) return false;
    node->sealed_content_ = encoded;
    return true;
}

bool FileSystem::set_transient_plain_content(FileNode* node, const std::string& content)
{
    if (!node || node->is_directory_ || node->sealed_content_.empty()) return false;
    if (content.size() != static_cast<std::size_t>(node->size_)) return false;
    node->content_ = content;
    return true;
}

void FileSystem::clear_sealed_content(FileNode* node)
{
    if (node && !node->is_directory_) node->sealed_content_.clear();
}

void FileSystem::wipe_sealed_plaintext()
{
    // 遍历的节点均来自已拥有的树或回收站，不解引用外部指针。
    const auto wipeOne = [](FileNode* node) {
        if (!node->sealed_content_.empty())
            PrivacyCrypto::wipe(node->content_);
    };
    traverse_recursive(root_, wipeOne);
    for (RecycleEntry* entry = recycle_head_; entry; entry = entry->next)
        traverse_recursive(entry->node, wipeOne);
}

bool FileSystem::set_metadata(FileNode *node,
                              long long size,
                              const std::string &created_time,
                              const std::string &modified_time)
{
    set_error(FileSystemError::NONE);

    if (node == nullptr || !owns_active_node(node)) {
        set_error(FileSystemError::INVALID_NODE);
        return false;
    }

    if (size < 0) {
        set_error(FileSystemError::INVALID_METADATA);
        return false;
    }

    node->size_ = size;
    node->created_time_ = created_time.empty() ? now_string() : created_time;
    node->modified_time_ = modified_time.empty() ? now_string() : modified_time;
    return true;
}


std::string FileSystem::get_path(FileNode *node) const
{
    if (node == nullptr || !owns_active_node(node)) {
        return std::string();
    }

    if (node == root_) {
        return "/";
    }

    std::string path;
    FileNode *current = node;

    while (current != nullptr && current != root_) {
        path = "/" + current->name_ + path;
        current = current->parent_;
    }

    return current == root_ ? path : std::string();
}


FileNode *FileSystem::get_root() const
{
    return root_;
}



bool FileSystem::contains_active_node(
    const FileNode* node) const
{
    if (node == nullptr || root_ == nullptr)
        return false;

    bool found = false;

    // 从真实根节点出发，只访问有效的活动树节点
    traverse(
        root_,
        [&](FileNode* current)
        {
            if (current == node)
                found = true;
        }
        );

    return found;
}

bool FileSystem::contains_recycle_root(
    const FileNode* node) const
{
    if (node == nullptr)
        return false;

    // 只比较地址，不访问 node->parent_ 等成员
    for (const RecycleEntry* entry = recycle_head_;
         entry != nullptr;
         entry = entry->next)
    {
        if (entry->node == node)
            return true;
    }

    return false;
}


void FileSystem::traverse(FileNode *start_node, const NodeVisitor &visitor) const
{
    if (start_node == nullptr || !owns_active_node(start_node) || !visitor) {
        return;
    }

    traverse_recursive(start_node, visitor);
}


bool FileSystem::is_in_recycle_bin(FileNode *node) const
{
    return find_recycle_entry(node) != nullptr;
}


std::string FileSystem::get_recycle_original_path(FileNode *node) const
{
    const RecycleEntry *entry = find_recycle_entry(node);
    return entry == nullptr ? std::string() : entry->original_path;
}


bool FileSystem::get_recycle_inherited_privacy(
    FileNode* node) const
{
    RecycleEntry* entry = find_recycle_entry(node);

    if (entry == nullptr)
        return false;

    return entry->inherited_privacy;
}

bool FileSystem::is_recycle_privacy_protected(
    FileNode* node) const
{
    if (node == nullptr)
        return false;

    // 从当前节点沿回收站内部父指针向上检查
    FileNode* root = node;

    for (FileNode* p = node;
         p != nullptr;
         p = p->get_parent())
    {
        if (p->is_locked())
            return true;

        root = p;
    }

    // root 是回收站顶层节点。
    // 只有确实存在对应回收站记录，才能查询继承保护。
    RecycleEntry* entry = find_recycle_entry(root);

    if (entry == nullptr)
        return false;

    return entry->inherited_privacy;
}



std::size_t FileSystem::get_recycle_count() const
{
    return recycle_count_;
}


void FileSystem::for_each_recycle_item(const RecycleVisitor &visitor) const
{
    if (!visitor) {
        return;
    }

    const RecycleEntry *entry = recycle_head_;
    while (entry != nullptr) {
        visitor(entry->node, entry->original_path);
        entry = entry->next;
    }
}


bool FileSystem::has_name_conflict(FileNode *parent,
                                   const std::string &name,
                                   FileNode *ignore_node) const
{
    if (parent == nullptr || !owns_active_node(parent) || !parent->is_directory_) {
        return true;
    }

    FileNode *current = parent->first_child_;
    while (current != nullptr) {
        if (current != ignore_node && current->name_ == name) {
            return true;
        }
        current = current->next_sibling_;
    }

    return false;
}


bool FileSystem::can_move(FileNode *node, FileNode *target_parent) const
{
    if (node == nullptr || target_parent == nullptr || node == root_ || !owns_active_node(node)
        || !owns_active_node(target_parent) || !target_parent->is_directory_) {
        return false;
    }

    if (node == target_parent || is_descendant(target_parent, node)) {
        return false;
    }

    return !has_name_conflict(target_parent, node->name_, node);
}


FileSystemError FileSystem::get_last_error() const
{
    return last_error_;
}


bool FileSystem::is_valid_name(const std::string& name)
{
    if (name.empty())
        return false;

    bool allWhitespace = true;

    for (char ch : name) {
        if (!std::isspace(static_cast<unsigned char>(ch))) {
            allWhitespace = false;
            break;
        }
    }

    return !allWhitespace;
}


std::string FileSystem::now_string()
{
    const std::time_t current_time = std::time(nullptr);
    const std::tm *local_time = std::localtime(&current_time);

    if (local_time == nullptr) {
        return std::string();
    }

    std::ostringstream stream;
    stream << std::put_time(local_time, "%Y-%m-%d %H:%M:%S");
    return stream.str();
}


void FileSystem::set_error(FileSystemError error)
{
    last_error_ = error;
}


bool FileSystem::owns_active_node(FileNode *node) const
{
    if (node == nullptr) {
        return false;
    }

    if (node == root_) {
        return true;
    }

    FileNode *current = node;
    while (current != nullptr && current != root_) {
        current = current->parent_;
    }

    return current == root_;
}


bool FileSystem::is_descendant(FileNode *node, FileNode *possible_ancestor) const
{
    if (node == nullptr || possible_ancestor == nullptr) {
        return false;
    }

    FileNode *current = node->parent_;
    while (current != nullptr) {
        if (current == possible_ancestor) {
            return true;
        }
        current = current->parent_;
    }

    return false;
}


void FileSystem::append_child(FileNode *parent, FileNode *node)
{
    if (parent == nullptr || node == nullptr) {
        return;
    }

    node->parent_ = parent;
    node->next_sibling_ = nullptr;

    if (parent->first_child_ == nullptr) {
        parent->first_child_ = node;
        parent->modified_time_ = now_string();
        return;
    }

    FileNode *current = parent->first_child_;
    while (current->next_sibling_ != nullptr) {
        current = current->next_sibling_;
    }

    current->next_sibling_ = node;
    parent->modified_time_ = now_string();
}


bool FileSystem::detach_node(FileNode *node)
{
    if (node == nullptr || node->parent_ == nullptr) {
        return false;
    }

    FileNode *parent = node->parent_;

    if (parent->first_child_ == node) {
        parent->first_child_ = node->next_sibling_;
        node->next_sibling_ = nullptr;
        node->parent_ = nullptr;
        parent->modified_time_ = now_string();
        return true;
    }

    FileNode *previous = parent->first_child_;
    while (previous != nullptr && previous->next_sibling_ != node) {
        previous = previous->next_sibling_;
    }

    if (previous == nullptr) {
        return false;
    }

    previous->next_sibling_ = node->next_sibling_;
    node->next_sibling_ = nullptr;
    node->parent_ = nullptr;
    parent->modified_time_ = now_string();
    return true;
}


FileNode *FileSystem::clone_subtree(FileNode *source, FileNode *new_parent)
{
    FileNode *copy = new FileNode(source->name_, source->is_directory_);
    const std::string current_time = now_string();

    // 复制语义：创建时间使用本次复制发生的时刻；
    // 修改时间沿用源节点，表示复制的是同一份已有内容/状态。
    copy->created_time_ = current_time;
    copy->modified_time_ = source->modified_time_;
    copy->size_ = source->size_;
    copy->content_ = source->content_;
    copy->sealed_content_ = source->sealed_content_;
    // 保留节点自身的锁定状态
    copy->is_locked_ = source->is_locked_;

    append_child(new_parent, copy);

    FileNode *child = source->first_child_;
    while (child != nullptr) {
        clone_subtree(child, copy);
        child = child->next_sibling_;
    }

    return copy;
}


void FileSystem::destroy_subtree(FileNode *node)
{
    if (node == nullptr) {
        return;
    }

    FileNode *child = node->first_child_;
    while (child != nullptr) {
        FileNode *next = child->next_sibling_;
        destroy_subtree(child);
        child = next;
    }

    delete node;
}

FileSystem::RecycleEntry *FileSystem::find_recycle_entry(FileNode *node) const
{
    RecycleEntry *entry = recycle_head_;
    while (entry != nullptr) {
        if (entry->node == node) {
            return entry;
        }
        entry = entry->next;
    }

    return nullptr;
}

void FileSystem::remove_recycle_entry(RecycleEntry *entry)
{
    if (entry == nullptr) {
        return;
    }

    if (recycle_head_ == entry) {
        recycle_head_ = entry->next;
        delete entry;
        --recycle_count_;
        return;
    }

    RecycleEntry *previous = recycle_head_;
    while (previous != nullptr && previous->next != entry) {
        previous = previous->next;
    }

    if (previous == nullptr) {
        return;
    }

    previous->next = entry->next;
    delete entry;
    --recycle_count_;
}


void FileSystem::traverse_recursive(FileNode *node, const NodeVisitor &visitor) const
{
    if (node == nullptr) {
        return;
    }

    visitor(node);

    FileNode *child = node->first_child_;
    while (child != nullptr) {
        FileNode *next = child->next_sibling_;
        traverse_recursive(child, visitor);
        child = next;
    }
}


bool FileSystem::set_pinned(FileNode* node, bool pinned)// 设置置顶
{
    if (node == nullptr ||
        !owns_active_node(node) ||
        node == root_)
    {
        set_error(FileSystemError::INVALID_NODE);
        return false;
    }

    // 置顶
    if (pinned && !node->pinned_)
    {
        node->pinned_ = true;
        node->pin_order_ = next_pin_order_++;
    }
    // 取消置顶
    else if (!pinned && node->pinned_)
    {
        node->pinned_ = false;
        node->pin_order_ = -1;
    }

    set_error(FileSystemError::NONE);
    return true;
}


bool FileSystem::is_pinned(FileNode* node) const // 检查置顶状态
{
    if (node == nullptr)
        return false;

    return node->pinned_;
}


bool FileSystem::undo(const Operation &operation)
{
    // 先检查撤销记录引用的节点是否仍然有效。
    switch (operation.type)
    {
    case OperationType::CREATE:
    case OperationType::RENAME:
    case OperationType::COPY:
    case OperationType::RESTORE:
        // 这些操作的撤销目标必须仍在活动文件树中
        if (!contains_active_node(operation.node))
            return false;
        break;

    case OperationType::DELETE:
        // 撤销删除：node 必须仍位于回收站顶层，
        // 原父目录必须仍在活动树中。
        if (!contains_recycle_root(operation.node) ||
            !contains_active_node(operation.old_parent))
            return false;
        break;

    case OperationType::MOVE:
        // 撤销移动：节点和原父目录都必须仍然有效。
        if (!contains_active_node(operation.node) ||
            !contains_active_node(operation.old_parent))
            return false;
        break;

    default:
        return false;
    }

    switch (operation.type)
    {
    case OperationType::CREATE:
        // 撤销创建：把刚创建的节点放入回收站。
        if (operation.node == nullptr)
            return false;

        return delete_node(operation.node);

    case OperationType::DELETE:
        // 撤销删除：从回收站恢复到原来的父目录。
        if (operation.node == nullptr ||
            operation.old_parent == nullptr)
        {
            return false;
        }

        return restore_node(
            operation.node,
            operation.old_parent
            );

    case OperationType::RENAME:
        // 撤销重命名：恢复原名称。
        if (operation.node == nullptr)
            return false;

        return rename_node(
            operation.node,
            operation.old_name
            );

    case OperationType::MOVE:
        // 撤销移动：移回原来的父目录。
        if (operation.node == nullptr ||
            operation.old_parent == nullptr)
        {
            return false;
        }

        return move_node(
            operation.node,
            operation.old_parent
            );

    case OperationType::COPY:
        // 撤销复制：把复制出来的节点放入回收站。
        if (operation.node == nullptr)
            return false;

        return delete_node(operation.node);

    case OperationType::RESTORE:
        // 撤销恢复：重新放入回收站。
        if (operation.node == nullptr)
            return false;

        return delete_node(operation.node);

    default:
        return false;
    }
}


void FileSystem::reset()
{
    // 清空回收站
    clear_recycle_bin();

    // 删除根目录下的全部正常节点
    FileNode *child = root_->first_child_;

    while (child != nullptr)
    {
        FileNode *next = child->next_sibling_;

        destroy_subtree(child);

        child = next;
    }

    root_->first_child_ = nullptr;

    // 当前目录回到根目录
    current_directory_ = root_;

    // 重置置顶顺序
    next_pin_order_ = 0;

    // 根节点恢复默认状态
    root_->pinned_ = false;
    root_->pin_order_ = -1;
    root_->size_ = 0;
    root_->content_.clear();

    root_->modified_time_ = now_string();

    set_error(FileSystemError::NONE);
}


// =========================
// 持久化接口
// =========================
bool FileSystem::restore_pin_state(FileNode* node,bool pinned,long long pin_order){
    set_error(FileSystemError::NONE);

    // 允许活动树节点，也允许回收站里的节点
    if (node == nullptr || (!owns_active_node(node) && !is_in_recycle_bin(node) &&
        !([&]() { FileNode* p = node; while (p->parent_ != nullptr) p = p->parent_; return is_in_recycle_bin(p); })())) {
        set_error(FileSystemError::INVALID_NODE);
        return false;
    }

    node->pinned_ = pinned;

    if (!pinned) {
        node->pin_order_ = -1;
        return true;
    }

    // 兼容旧存档：旧格式可能没有 pin_order
    if (pin_order < 0) {
        node->pin_order_ = next_pin_order_++;
        return true;
    }

    node->pin_order_ = pin_order;

    if (pin_order >= next_pin_order_) {
        next_pin_order_ = pin_order + 1;
    }

    return true;
}


FileNode* FileSystem::create_recycle_node_for_load(
    FileNode* recycle_parent,
    const std::string& name,
    bool is_directory,
    const std::string& original_path,
    bool inherited_privacy)
{
    set_error(FileSystemError::NONE);

    if (!is_valid_name(name)) {
        set_error(FileSystemError::INVALID_NAME);
        return nullptr;
    }

    FileNode* node = new FileNode(name, is_directory);

    // 情况1：回收站顶层节点
    if (recycle_parent == nullptr) {
        RecycleEntry* entry = new RecycleEntry(
            node,
            original_path,
            recycle_head_,
            inherited_privacy
            );

        recycle_head_ = entry;
        ++recycle_count_;

        return node;
    }

    // 情况2：回收站文件夹内部的子节点
    node->parent_ = recycle_parent;

    if (recycle_parent->first_child_ == nullptr) {
        recycle_parent->first_child_ = node;
    }
    else {
        FileNode* child = recycle_parent->first_child_;

        while (child->next_sibling_ != nullptr) {
            child = child->next_sibling_;
        }

        child->next_sibling_ = node;
    }

    return node;
}


bool FileSystem::restore_recycle_data(FileNode* node, const std::string& content,
                                      long long size, const std::string& created_time,
                                      const std::string& modified_time)
{
    set_error(FileSystemError::NONE);
    if (node == nullptr || size < 0) {
        set_error(FileSystemError::INVALID_METADATA);
        return false;
    }
    // 子节点不是 RecycleEntry 的顶层项：沿父节点向上查找回收站根。
    FileNode* root = node;
    while (root->parent_ != nullptr)
        root = root->parent_;
    if (!is_in_recycle_bin(root)) {
        set_error(FileSystemError::NOT_IN_RECYCLE_BIN);
        return false;
    }
    if (!node->is_directory_)
        node->content_ = content;
    node->size_ = size;
    node->created_time_ = created_time.empty() ? now_string() : created_time;
    node->modified_time_ = modified_time.empty() ? now_string() : modified_time;
    return true;
}


bool FileSystem::restore_recycle_lock_state(
    FileNode* node, bool locked)
{
    set_error(FileSystemError::NONE);

    if (node == nullptr) {
        set_error(FileSystemError::NOT_IN_RECYCLE_BIN);
        return false;
    }
    FileNode* root = node;
    while (root->parent_ != nullptr) root = root->parent_;
    if (!is_in_recycle_bin(root)) {
        set_error(FileSystemError::NOT_IN_RECYCLE_BIN);
        return false;
    }

    node->is_locked_ = locked;
    return true;
}


// =========================
// 文件锁状态管理
// =========================
bool FileSystem::set_locked(FileNode* node, bool locked)
{
    set_error(FileSystemError::NONE);

    // 只能操作正常文件树中的有效节点
    if (node == nullptr || !owns_active_node(node)) {
        set_error(FileSystemError::INVALID_NODE);
        return false;
    }

    // 根目录不允许上锁
    if (node == root_) {
        set_error(FileSystemError::ROOT_OPERATION);
        return false;
    }

    node->is_locked_ = locked;
    return true;
}

bool FileSystem::is_locked(FileNode* node) const
{
    if (node == nullptr || !owns_active_node(node))
        return false;

    return node->is_locked_;
}

bool FileSystem::restore_lock_state(FileNode* node, bool locked)
{
    return set_locked(node, locked);
}