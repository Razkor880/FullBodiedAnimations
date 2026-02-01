class FBTransform {
public:
    static void ApplyScale(RE::Actor* actor, std::string_view nodeName, float scale);
    static void ApplyScale_MainThread(RE::Actor* actor, std::string_view nodeName, float scale);

    static bool TryGetScale(RE::Actor* actor, std::string_view nodeName, float& outScale);
};
