#pragma once

#include "node_base.h"

std::shared_ptr<BaseNode> createNode(NodeType type, NodeObserver* observer, const std::string& id = "");

class NodeFactory {
public:
    static std::shared_ptr<BaseNode> createNode(NodeType type, NodeObserver* observer, const std::string& id = "");
};
