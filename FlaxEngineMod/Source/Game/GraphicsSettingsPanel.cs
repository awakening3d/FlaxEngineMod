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

    private bool IsPanelVisible = false;

    private bool _wasSwitchPressed;
    private Button _btnHDR;
	
	private const float maxNitsExposure = 10;
	private const float minNitsExposure = 300;
	
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
            if (child is UIControl ui && ui.Control is Button btn)
            {
                if (ui.Name == "btnHDR") _btnHDR = btn;
                btn.Clicked += () => OnButtonClicked(ui.Name);
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
            default: Debug.LogWarning($"Unhandled button: {name}"); break;
        }
    }
	
	//统一的滑块事件处理中心
    private void OnSliderChanged(string name, Slider slider)
    {
		float t = slider.Value*0.01f; //[0-1]
        switch (name)
        {
            case "sldExposure":
                // 这里写针对 Exposure 的处理
				var settings = Graphics.PostProcessSettings;
				settings.FakeHDRs.NitsExposure = Mathf.Lerp( maxNitsExposure, minNitsExposure, Curves.EaseOut(t) ) *0.01f;
				Graphics.PostProcessSettings = settings;
				Debug.LogWarning($"on slider exposure change {settings.FakeHDRs.NitsExposure}"); break;
                break;
                
            case "SliderFOV":
                        // 这里写针对 FOV 的处理
                break;
                
            default:
                Debug.LogWarning($"Unhandled slider: {name}");
                break;
        }
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
                    // 获取当前的 Nits 值
                    float currentNits = Graphics.PostProcessSettings.FakeHDRs.NitsExposure * 100f;
                    
                    // 因为我们用的是 EaseOut，逆推有一点复杂。
                    // 为了 UI 初始位置准确，最简单的办法是直接用一个线性反向映射来给个大概，
                    // 或者如果以后参数复杂了，最好直接保存一个对应的 UI 进度变量。
                    
                    // 这里我们简单按线性反向算一下：
                    // Nits 从 maxNitsExposure 到 minNitsExposure，反向就是 minNitsExposure 到 maxNitsExposure
                    float normalized = (maxNitsExposure - currentNits) / (maxNitsExposure - minNitsExposure); 
                    // 如果用了 EaseOut 映射，反向计算 t = 1 - cbrt(1 - normalized)
                    float t = 1f - MathF.Cbrt(1f - normalized);
                    
                    // 给滑块赋值 [0-100]
                    slider.Value = Mathf.Clamp(t * 100f, 0f, 100f);
                }
            }
            SyncSlidersFromSettings(child);
        }
    }

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