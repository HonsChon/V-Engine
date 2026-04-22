#include "Entity.h"

namespace VulkanEngine {

Entity::Entity(entt::entity handle, Scene* scene)
    : m_entityHandle(handle), m_scene(scene) {
}

// ============================================================
// 层级关系
// ============================================================

void Entity::setParent(Entity parent) {
    if (!*this || !parent) return;
    if (*this == parent) return;  // 不能将自己设为父节点
    
    // 确保两个实体都有 RelationshipComponent
    if (!hasComponent<RelationshipComponent>()) {
        addComponent<RelationshipComponent>();
    }
    if (!parent.hasComponent<RelationshipComponent>()) {
        parent.addComponent<RelationshipComponent>();
    }
    
    auto& myRelationship = getComponent<RelationshipComponent>();
    auto& parentRelationship = parent.getComponent<RelationshipComponent>();
    
    // 如果已经有父节点，先从原父节点移除
    if (myRelationship.parent != entt::null) {
        Entity oldParent(myRelationship.parent, m_scene);
        oldParent.removeChild(*this);
    }
    
    // 设置新的父节点
    myRelationship.parent = parent.getHandle();
    
    // 将自己添加到新父节点的子节点链表
    if (parentRelationship.firstChild == entt::null) {
        // 父节点没有子节点，直接设置为第一个子节点
        parentRelationship.firstChild = m_entityHandle;
    }
    else {
        // 找到最后一个子节点
        Entity lastChild(parentRelationship.firstChild, m_scene);
        auto& lastChildRelationship = lastChild.getComponent<RelationshipComponent>();
        
        while (lastChildRelationship.nextSibling != entt::null) {
            lastChild = Entity(lastChildRelationship.nextSibling, m_scene);
            lastChildRelationship = lastChild.getComponent<RelationshipComponent>();
        }
        
        // 添加到链表末尾
        lastChildRelationship.nextSibling = m_entityHandle;
        myRelationship.prevSibling = lastChild.getHandle();
    }
    
    parentRelationship.childrenCount++;
}

Entity Entity::getParent() const {
    if (!*this) return Entity();
    
    if (!hasComponent<RelationshipComponent>()) {
        return Entity();
    }
    
    const auto& relationship = getComponent<RelationshipComponent>();
    if (relationship.parent == entt::null) {
        return Entity();
    }
    
    return Entity(relationship.parent, m_scene);
}

std::vector<Entity> Entity::getChildren() const {
    std::vector<Entity> children;
    
    if (!*this || !hasComponent<RelationshipComponent>()) {
        return children;
    }
    
    const auto& relationship = getComponent<RelationshipComponent>();
    
    entt::entity childHandle = relationship.firstChild;
    while (childHandle != entt::null) {
        Entity child(childHandle, m_scene);
        children.push_back(child);
        
        if (child.hasComponent<RelationshipComponent>()) {
            childHandle = child.getComponent<RelationshipComponent>().nextSibling;
        }
        else {
            break;
        }
    }
    
    return children;
}

void Entity::addChild(Entity child) {
    if (!*this || !child) return;
    if (*this == child) return;
    
    child.setParent(*this);
}

void Entity::removeChild(Entity child) {
    if (!*this || !child) return;
    
    if (!hasComponent<RelationshipComponent>() || !child.hasComponent<RelationshipComponent>()) {
        return;
    }
    
    auto& myRelationship = getComponent<RelationshipComponent>();
    auto& childRelationship = child.getComponent<RelationshipComponent>();
    
    // 确认是父子关系
    if (childRelationship.parent != m_entityHandle) {
        return;
    }
    
    // 从兄弟链表中移除
    if (childRelationship.prevSibling != entt::null) {
        Entity prev(childRelationship.prevSibling, m_scene);
        prev.getComponent<RelationshipComponent>().nextSibling = childRelationship.nextSibling;
    }
    else {
        // 是第一个子节点
        myRelationship.firstChild = childRelationship.nextSibling;
    }
    
    if (childRelationship.nextSibling != entt::null) {
        Entity next(childRelationship.nextSibling, m_scene);
        next.getComponent<RelationshipComponent>().prevSibling = childRelationship.prevSibling;
    }
    
    // 清除子节点的关系
    childRelationship.parent = entt::null;
    childRelationship.prevSibling = entt::null;
    childRelationship.nextSibling = entt::null;
    
    myRelationship.childrenCount--;
}

bool Entity::hasParent() const {
    if (!*this || !hasComponent<RelationshipComponent>()) {
        return false;
    }
    return getComponent<RelationshipComponent>().parent != entt::null;
}

bool Entity::hasChildren() const {
    if (!*this || !hasComponent<RelationshipComponent>()) {
        return false;
    }
    return getComponent<RelationshipComponent>().childrenCount > 0;
}

} // namespace VulkanEngine
