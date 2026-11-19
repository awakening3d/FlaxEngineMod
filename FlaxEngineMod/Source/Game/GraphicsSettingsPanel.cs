using FlaxEngine;
using FlaxEngine.GUI;
using CurveRemap;
using System;

public class GraphicsSettingsPanel : Script
{
    [Tooltip("要开关的 UI 画布 Actor（在编辑器中拖入 UICanvas）。")]
    public UICanvas Canvas;

    [Tooltip("Input action name to toggle the canvas (configured in Input Settings).")]
    public string ToggleActionName = "SwitchDebugInfo";

    [Tooltip("整体画面后处理")]
    public PostFxVolume postfx;

    private bool IsPanelVisible = false;

    private bool _wasSwitchPressed;
    private Button _btnHDR;
	private Label _lblExposure; // Exposure 数值展示 Label
	private Label _lblBrightness; // Brightness 数值展示 Label
	private Label _lblContrast;	// Contrast 数值展示 Label
	
    private const float maxNitsExposure = 10;
    private const float minNitsExposure = 300;

    // 颜色分级滑块映射的统一范围 [0, 2], 1 为中性值
    private const float ColorGradingMin = 0f;
    private const float ColorGradingMax = 2f;

    // 颜色分级可调节字段
    private enum ColorGradingField { Contrast, Gain }
	
	
	
	// 一次性重置三个值为常数, 并同步滑块位置
	private void ResetPostSettings()
	{
		// Exposure: NitsExposure
		var settings = Graphics.PostProcessSettings;
		settings.FakeHDRs.NitsExposure = 1.5f;
		Graphics.PostProcessSettings = settings;

		// Contrast / Brightness
		var grading = postfx.ColorGrading;
		grading.ColorContrast.W = 1.45f;
		grading.ColorGain.W = 1f;
		postfx.ColorGrading = grading;

		// 同步滑块 UI 位置
		SyncSlidersFromSettings(Canvas);
	}
	

    private void SetPanelVisible(bool visible)
    {
        IsPanelVisible = visible;
        if (Canvas != null) Canvas.IsActive = IsPanelVisible;
        UIManager.IsUIOpen = IsPanelVisible;
    }

    public override void OnEnable()
    {
        if (Canvas != null) Canvas.IsActive = IsPanelVisible;
        if (Canvas != null)
        {
            HookButtons(Canvas);
            HookSliders(Canvas);
        }
        UpdateButtonText();
        // 初始化滑块 UI 位置
        SyncSlidersFromSettings(Canvas);
    }

	private void HookButtons(Actor root)
	{
		foreach (var child in root.Children)
		{
			if (child is UIControl ui)
			{
				if (ui.Control is Button btn)
				{
					if (ui.Name == "btnHDR") _btnHDR = btn;
					btn.Clicked += () => OnButtonClicked(ui.Name);
				}
				else if (ui.Control is Label lbl)
				{
					if (ui.Name == "labelExposure") _lblExposure = lbl;
					else if (ui.Name == "labelBrightness") _lblBrightness = lbl;
					else if (ui.Name == "labelContrast") _lblContrast = lbl;
				}
			}
			HookButtons(child);
		}
	}

    // 自动遍历挂载滑块
    private void HookSliders(Actor root)
    {
        foreach (var child in root.Children)
        {
            if (child is UIControl ui && ui.Control is Slider slider)
            {
                slider.ValueChanged += () => OnSliderChanged(ui.Name, slider);
            }
            HookSliders(child);
        }
    }

    private void OnButtonClicked(string name)
    {
        switch (name)
        {
            case "btnClose":
                SetPanelVisible(false);
                break;
            case "btnHDR":
                ToggleHDR();
                break;
			case "btnReset":
				ResetPostSettings();
				break;				
            default: Debug.LogWarning($"Unhandled button: {name}"); break;
        }
    }
	
	// 刷新三个 Label 的数值显示, 格式 x.x
	private void RefreshLabels()
	{
		if (_lblExposure != null)
			_lblExposure.Text = (Graphics.PostProcessSettings.FakeHDRs.NitsExposure).ToString("F2");

		if (_lblBrightness != null)
			_lblBrightness.Text = postfx.ColorGrading.ColorGain.W.ToString("F2");

		if (_lblContrast != null)
			_lblContrast.Text = postfx.ColorGrading.ColorContrast.W.ToString("F2");
	}

    // 统一的滑块事件处理中心
    private void OnSliderChanged(string name, Slider slider)
    {
        float t = slider.Value * 0.01f; // [0-1]
        switch (name)
        {
            case "sldExposure":
                var settings = Graphics.PostProcessSettings;
                settings.FakeHDRs.NitsExposure = Mathf.Lerp(maxNitsExposure, minNitsExposure, Curves.EaseOut(t)) * 0.01f;
                Graphics.PostProcessSettings = settings;
                break;

            case "sldContrast":
                ApplyColorGradingW(ColorGradingField.Contrast, SliderToColorGrading(t));
                break;

            case "sldBrightness":
                ApplyColorGradingW(ColorGradingField.Gain, SliderToColorGrading(t));
                break;

            default:
                Debug.LogWarning($"Unhandled slider: {name}");
                break;
        }
		RefreshLabels(); //数值显示
    }

	// 遍历找到滑块并同步初始值
	private void SyncSlidersFromSettings(Actor root)
	{
		foreach (var child in root.Children)
		{
			if (child is UIControl ui && ui.Control is Slider slider)
			{
				if (ui.Name == "sldExposure")
				{
					float currentNits = Graphics.PostProcessSettings.FakeHDRs.NitsExposure * 100f;
					float normalized = (maxNitsExposure - currentNits) / (maxNitsExposure - minNitsExposure);
					// EaseOut(t) = 1 - (1-t)^3, 反向 t = 1 - cbrt(1 - normalized)
					float tInv = 1f - MathF.Cbrt(1f - normalized);
					slider.Value = Mathf.Clamp(tInv * 100f, 0f, 100f);
				}
				else if (ui.Name == "sldBrightness")
				{
					float currentGain = postfx.ColorGrading.ColorGain.W;
					slider.Value = ColorGradingToSlider(currentGain) * 100f;
				}
				else if (ui.Name == "sldContrast")
				{
					float currentContrast = postfx.ColorGrading.ColorContrast.W;
					slider.Value = ColorGradingToSlider(currentContrast) * 100f;
				}
			}
			SyncSlidersFromSettings(child);
		}
	}

    // 滑块进度 t in [0,1] 到颜色分级参数 [0,2], 带 EaseOut
    private static float SliderToColorGrading(float t)
    {
		return Mathf.Lerp(ColorGradingMin, ColorGradingMax, t);
        //return Mathf.Lerp(ColorGradingMin, ColorGradingMax, Curves.EaseOut(t));
    }

	// 颜色分级参数到滑块进度 [0,1], EaseOut 的反向
	private static float ColorGradingToSlider(float value)
	{
		return Mathf.Clamp( (value - ColorGradingMin) / (ColorGradingMax - ColorGradingMin), 0f, 1f);
		//float normalized = Mathf.Clamp( (value - ColorGradingMin) / (ColorGradingMax - ColorGradingMin), 0f, 1f);
		// EaseOut(t) = 1 - (1-t)^3, 反向 t = 1 - cbrt(1 - normalized)
		//return 1f - MathF.Cbrt(1f - normalized);
	}


    // 写回 ColorGrading 的 W 字段
    private void ApplyColorGradingW(ColorGradingField field, float wValue)
    {
        var grading = postfx.ColorGrading;
        switch (field)
        {
            case ColorGradingField.Contrast: grading.ColorContrast.W = wValue; break;
            case ColorGradingField.Gain:     grading.ColorGain.W = wValue;     break;
        }
        postfx.ColorGrading = grading;
    }

    // ---------- 按钮 ----------

    private void ToggleHDR()
    {
        Graphics.EnableHDROutput = !Graphics.EnableHDROutput;
        Screen.ResetSwapChain();
        UpdateButtonText();
    }

    private void UpdateButtonText()
    {
        if (_btnHDR != null)
            _btnHDR.Text = Graphics.EnableHDROutput ? "HDR: On" : "HDR: Off";
    }

    public override void OnUpdate()
    {
        bool isSwitchPressed = Input.GetAction(ToggleActionName);
        if (isSwitchPressed && !_wasSwitchPressed)
        {
            SetPanelVisible(!IsPanelVisible);
        }
        _wasSwitchPressed = isSwitchPressed;
    }
}