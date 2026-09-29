#ifndef OPERATION_H
#define OPERATION_H

#include <cstddef>
#include <string>

class FileNode;

enum class OperationType
{
    CREATE,
    DELETE,
    RENAME,
    MOVE,
    COPY,
    RESTORE
};

// 一条撤销记录。
// node / old_parent / new_parent 等均为非拥有指针：Operation 不负责释放 FileNode。
class Operation
{
public:
    Operation();

    OperationType type;
    FileNode* node;

    // RENAME
    std::string old_name;
    std::string new_name;

    // MOVE
    FileNode* old_parent;
    FileNode* new_parent;

    // DELETE / RESTORE / MOVE 等可选路径信息。
    std::string old_path;
    std::string new_path;
};

// 链式栈，供撤销模块直接使用。
class OperationStack
{
public:
    OperationStack();
    ~OperationStack();

    OperationStack(const OperationStack&) = delete;
    OperationStack& operator=(const OperationStack&) = delete;

    void push(const Operation& operation);
    bool pop(Operation& operation);
    bool peek(Operation& operation) const;
    bool empty() const;
    std::size_t size() const;
    void clear();

private:
    struct Node
    {
        Operation operation;
        Node* next;

        Node(const Operation& value, Node* next_node)
            : operation(value), next(next_node)
        {
        }
    };

    Node* top_;
    std::size_t size_;
};

#endif // OPERATION_H
