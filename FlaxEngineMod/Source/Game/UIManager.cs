using FlaxEngine;
using FlaxEngine.GUI;

public class UIManager : Script
{
    [Header("Gamepad Cursor Settings")]
    public float CursorSpeed = 300f;
    public float Deadzone = 0.15f;

    // 内部单例
    private static UIManager _instance;

    // 外部依然可以写 UIManager.IsUIOpen = true;
    public static bool IsUIOpen
    {
        get => _instance != null && _instance._isUIOpen;
        set
        {
            if (_instance != null && _instance._isUIOpen != value)
            {
                _instance._isUIOpen = value;
                Screen.CursorVisible = value;
                Screen.CursorLock = value ? CursorLockMode.None : CursorLockMode.Locked;
            }
        }
    }

    private bool _isUIOpen = false;
    private Float2 _virtualMousePos = Float2.Zero;
    private bool _wasAPressed = false;

    public override void OnEnable()
    {
		FlaxEngine.GUI.CanvasRootControl.DisableNavigation = true;
        _instance = this;
    }

    public override void OnDisable()
    {
        if (_instance == this) _instance = null;
    }

    
	public override void OnUpdate()
	{
		if (!_isUIOpen) return;

		var guiRoot = RootControl.GameRoot;
		if (guiRoot == null) return;

		var gamepad = Input.GetGamepad(0);
		
		// 1. 手柄摇杆移动光标
		if (gamepad != null)
		{
			float axisX = gamepad.GetAxis(GamepadAxis.LeftStickX);
			float axisY = gamepad.GetAxis(GamepadAxis.LeftStickY);

			if (Mathf.Abs(axisX) > Deadzone || Mathf.Abs(axisY) > Deadzone)
			{
				// 如果虚拟坐标未初始化，先同步到当前物理鼠标位置
				if (_virtualMousePos == Float2.Zero) 
					_virtualMousePos = Input.MousePosition;

				// 计算位移
				float speed = CursorSpeed * Time.DeltaTime;
				_virtualMousePos.X += axisX * speed;
				_virtualMousePos.Y -= axisY * speed;

				// 限制在屏幕范围内
				_virtualMousePos.X = Mathf.Clamp(_virtualMousePos.X, 0, Screen.Size.X);
				_virtualMousePos.Y = Mathf.Clamp(_virtualMousePos.Y, 0, Screen.Size.Y);

				// 强行把物理光标移到虚拟坐标处（引擎底层会自动派发 MouseMove 事件）
				Input.MousePosition = _virtualMousePos; 
			}
			else
			{
				// 如果手柄没动，让虚拟坐标跟随物理鼠标（支持鼠标、手柄无缝切换）
				_virtualMousePos = Input.MousePosition;
			}


			// 2. 处理 A 键模拟鼠标左键点击
			bool isAPressed = gamepad.GetButton(GamepadButton.A);
			if (isAPressed && !_wasAPressed)
			{
				guiRoot.OnMouseDown(_virtualMousePos, MouseButton.Left);
			}
			else if (!isAPressed && _wasAPressed)
			{
				guiRoot.OnMouseUp(_virtualMousePos, MouseButton.Left);
			}
			_wasAPressed = isAPressed;

		}
	}
	
	
}