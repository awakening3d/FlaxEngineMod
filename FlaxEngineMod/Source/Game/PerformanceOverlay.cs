using System;
using FlaxEngine;
using FlaxEngine.GUI;

public class PerformanceOverlay : Script
{
    [Tooltip("Toggle the visibility of the performance overlay.")]
    public bool IsOverlayVisible = true;

    [Tooltip("Color of the performance text.")]
    public Color TextColor = Color.White;

    [Tooltip("Input action name to toggle the overlay (configured in Input Settings).")]
    public string ToggleActionName = "SwitchDebugInfo";
    private bool _wasSwitchPressed = false;

    [Tooltip("Input action name to toggle fullscreen/window (configured in Input Settings).")]
    public string SwitchFullscreenActionName = "SwitchFullscreen";
    private bool _wasSwitchFullscreenPressed = false;


	public class NonBlockingLabel : Label
	{
		public override bool ContainsPoint(ref Float2 location, bool precise = false) => false;
		public override bool IntersectsContent(ref Float2 locationParent, out Float2 location)
		{
			location = Float2.Zero;
			return false;
		}
	}



    private NonBlockingLabel _label;

    // 常用分辨率预设
    private readonly Float2[] _resolutions = new Float2[]
    {
        new Float2(1280, 720),
        new Float2(1600, 900),
        new Float2(1920, 1080),
        new Float2(2560, 1440),
        new Float2(3840, 2160),
    };
	
    private int _currentResolutionIndex = 2;
    private bool _wasPgDnPressed = false;
    private bool _wasPgUpPressed = false;
	

    public override void OnEnable()
    {
        _label = new NonBlockingLabel
        {
            Text = "",
            Location = new Vector2(5, 5),
            Size = new Vector2(300, 300),
            HorizontalAlignment = TextAlignment.Near,
            VerticalAlignment = TextAlignment.Near,
            TextColor = TextColor,
            AutoFocus = false,
            Visible = IsOverlayVisible,
        };

        _label.Parent = RootControl.GameRoot;

		// 强制开启 GPU 性能分析
		ProfilerGPU.Enabled = true;		
    }

    public override void OnDisable()
    {
        if (_label != null)
        {
            _label.Dispose();
            _label = null;
        }
    }

    public override void OnUpdate()
    {
		
		// toggle fps info visible
        bool isSwitchPressed = Input.GetAction(ToggleActionName);
        if (isSwitchPressed && !_wasSwitchPressed)
        {
            IsOverlayVisible = !IsOverlayVisible;
            if (_label != null)
                _label.Visible = IsOverlayVisible;
        }
        _wasSwitchPressed = isSwitchPressed;
		
		// toggle fullscreen/window
        bool isSwitchFullscreenPressed = Input.GetAction(SwitchFullscreenActionName);
        if (isSwitchFullscreenPressed && !_wasSwitchFullscreenPressed)
        {
			if (GameWindowMode.FullscreenBorderless == Screen.GameWindowMode)
				Screen.GameWindowMode = GameWindowMode.Windowed;
			else
				Screen.GameWindowMode = GameWindowMode.FullscreenBorderless;
        }
        _wasSwitchFullscreenPressed = isSwitchFullscreenPressed;
		

        // PageDown 往下切换分辨率
        bool isPgDnPressed = Input.GetKey(KeyboardKeys.PageDown);
        if (isPgDnPressed && !_wasPgDnPressed)
        {
            _currentResolutionIndex = (_currentResolutionIndex + 1) % _resolutions.Length;
            Screen.Size = _resolutions[_currentResolutionIndex];
            //Debug.Log($"Resolution switched to: {_resolutions[_currentResolutionIndex].X}x{_resolutions[_currentResolutionIndex].Y}");
        }
        _wasPgDnPressed = isPgDnPressed;

        // PageUp 往回切换分辨率
        bool isPgUpPressed = Input.GetKey(KeyboardKeys.PageUp);
        if (isPgUpPressed && !_wasPgUpPressed)
        {
            _currentResolutionIndex = (_currentResolutionIndex - 1 + _resolutions.Length) % _resolutions.Length;
            Screen.Size = _resolutions[_currentResolutionIndex];
            //Debug.Log($"Resolution switched to: {_resolutions[_currentResolutionIndex].X}x{_resolutions[_currentResolutionIndex].Y}");
        }
        _wasPgUpPressed = isPgUpPressed;

        if (!IsOverlayVisible || _label == null) return;

        var stats = ProfilingTools.Stats;
        var drawStats = stats.DrawStats;
        var task = MainRenderTask.Instance;
		
        // 获取当前窗口/渲染分辨率
        var size = Screen.Size;
        int width = (int)(size.X * task.RenderScale);
        int height = (int)(size.Y * task.RenderScale);
		

        System.Text.StringBuilder sb = new System.Text.StringBuilder();
		sb.AppendLine($"{(Graphics.EnableHDROutput ? "HDR " : "")}Resolution: {width}x{height} (Scale: {task.RenderScale:F2})");
        sb.AppendLine($"FPS: {stats.FPS}");
        sb.AppendLine($"Update CPU: {stats.UpdateTimeMs:F2} ms");
        sb.AppendLine($"Draw CPU: {stats.DrawCPUTimeMs:F2} ms");
        sb.AppendLine($"Draw GPU: {stats.DrawGPUTimeMs:F2} ms");
        sb.AppendLine($"Draw Calls: {drawStats.DrawCalls}");
        sb.AppendLine($"Triangles: {drawStats.Triangles}");
        sb.AppendLine($"Vertices: {drawStats.Vertices}");
        sb.AppendLine($"Render Scale: {task.RenderScale:F2}");
        sb.AppendLine($"Managed Mem: {GC.GetTotalMemory(false) / (1024f * 1024f):F0} MB");

        _label.Text = sb.ToString();
    }
}