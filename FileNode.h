#ifndef FILENODE_H
#define FILENODE_H

#include <string>

class FileSystem;

// 文件系统中的统一节点。
// 使用孩子-兄弟表示法：
// parent_       -> 父节点
// first_child_  -> 第一个子节点
// next_sibling_ -> 同级下一个节点
class FileNode
{
public:
    FileNode(const std::string &name, bool is_directory);
    ~FileNode();

    // 只读访问接口。其他模块不要直接修改节点内部数据。
    const std::string &get_name() const;
    bool is_directory() const;
    long long get_size() const;
    const std::string &get_created_time() const;
    const std::string &get_modified_time() const;
    const std::string &get_content() const;

    FileNode *get_parent() const;
    FileNode *get_first_child() const;
    FileNode *get_next_sibling() const;

private:
    std::string name_;
    bool is_directory_;
    long long size_;
    std::string created_time_;
    std::string modified_time_;
    std::string content_;

    FileNode *parent_;
    FileNode *first_child_;
    FileNode *next_sibling_;

    // 只有 FileSystem 负责修改树结构和节点属性。
    friend class FileSystem;
};

#endif // FILENODE_H
