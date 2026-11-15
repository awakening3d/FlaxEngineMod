using FlaxEngine;
using FlaxEngine.GUI;

public class GraphicsSettingsPanel : Script
{
    [Tooltip("要开关的 UI 画布 Actor（在编辑器中拖入 UICanvas）。")]
    public UICanvas Canvas;

    [Tooltip("Input action name to toggle the canvas (configured in Input Settings).")]
    public string ToggleActionName = "SwitchDebugInfo";

    private bool IsPanelVisible = false;

    private bool _wasSwitchPressed;
    private Button _btnHDR;
	
	private void SetPanelVisible(bool visible)
	{
		IsPanelVisible = visible;
		if (Canvas != null) Canvas.IsActive = IsPanelVisible;
		UIManager.IsUIOpen = IsPanelVisible; 
	}

    public override void OnEnable()
    {
        if (Canvas != null) Canvas.IsActive = IsPanelVisible;
        if (Canvas != null) HookButtons(Canvas);
        UpdateButtonText();
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