#include "FileSystem.h"

#include <ctime>
#include <iomanip>
#include <sstream>

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

    recycle_head_ = new RecycleEntry(node, original_path, recycle_head_);
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

    if (path == "/" || path == ".") {
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

bool FileSystem::is_valid_name(const std::string &name)
{
    return !name.empty() && name != "." && name != ".." && !contains_forbidden_name_character(name);
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


bool FileSystem::set_pinned(FileNode* node, bool pinned)
{
    if (node == nullptr)
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


bool FileSystem::is_pinned(FileNode* node) const
{
    if (node == nullptr)
        return false;

    return node->pinned_;
}

