using System;

namespace CurveRemap
{
    public static class Curves
    {
        // 1. 线性 (Linear) - 匀速，没有加速减速
        public static float Linear(float t) => t;

        // 2. 慢入 (Ease-In) - 起步极慢，然后猛冲（类似 t^3）
        // 适合：蓄力条、刚开始缓慢然后加速的参数
        public static float EaseIn(float t) => t * t * t;

        // 3. 慢出 (Ease-Out) - 起步极快，末端极慢
        // 适合：摄像机急停、UI 弹出效果、需要快速响应但平滑停止的参数
        public static float EaseOut(float t) => 1f - MathF.Pow(1f - t, 3f);

        // 4. 慢进慢出平滑，两端平缓，中间剧烈
        // 适合：摄像机移动，起步收尾平稳
        public static float SmoothStep(float t) => t * t * (3f - 2f * t);

        // 5. 极平滑 (SmootherStep) - 比 SmoothStep 更平滑（两端导数为 0，加速度也为 0）
        // 适合：要求极度丝滑、没有一丝突兀的过渡
        public static float SmootherStep(float t) => t * t * t * (t * (t * 6f - 15f) + 10f);

        // 6. 回弹 (Ease-Out Back) - 会稍微冲过头然后再弹回来
        // 适合：UI 弹性动画，或者你想让参数有“过冲”再回落的效果
        public static float EaseOutBack(float t)
        {
            float c1 = 1.70158f;
            float c3 = c1 + 1f;
            return 1f + c3 * MathF.Pow(t - 1f, 3f) + c1 * MathF.Pow(t - 1f, 2f);
        }

        // 7. 正弦缓出 (Sine Ease-Out) - 极其柔和的减速
        // 适合：柔和的过渡，比如夜晚转白天、场景渐入
        public static float SineEaseOut(float t) => MathF.Sin((t * MathF.PI) / 2f);

        // 8. 指数映射 (Exponential) - 适合音量和亮度的映射
        // 因为人耳/人眼对声音和亮度的感知是对数的，指数曲线更符合直觉
        // p 是幂次，比如 2.0f、3.0f
        public static float Exponential(float t, float p=2.0f) => MathF.Pow(t, p);
    }
}