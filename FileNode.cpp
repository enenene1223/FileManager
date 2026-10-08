#include "FileNode.h"

FileNode::FileNode(const std::string &name, bool is_directory)
    : name_(name)
    , is_directory_(is_directory)
    , pinned_(false)
    , size_(0)
    , pin_order_(-1)
    , created_time_()
    , modified_time_()
    , content_()
    , parent_(nullptr)
    , first_child_(nullptr)
    , next_sibling_(nullptr)
{}

FileNode::~FileNode() = default;

const std::string &FileNode::get_name() const
{
    return name_;
}

bool FileNode::is_directory() const
{
    return is_directory_;
}

bool FileNode::is_pinned() const
{
    return pinned_;
}

void FileNode::set_pinned(bool pinned) {
    pinned_ = pinned;
}

long long FileNode::get_size() const
{
    return size_;
}

long long FileNode::get_pin_order() const
{
    return pin_order_;
}

const std::string &FileNode::get_created_time() const
{
    return created_time_;
}

const std::string &FileNode::get_modified_time() const
{
    return modified_time_;
}

const std::string &FileNode::get_content() const
{
    return content_;
}

FileNode *FileNode::get_parent() const
{
    return parent_;
}

FileNode *FileNode::get_first_child() const
{
    return first_child_;
}

FileNode *FileNode::get_next_sibling() const
{
    return next_sibling_;
}
