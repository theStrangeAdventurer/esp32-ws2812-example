import { render, h } from 'preact';
import { useState, useRef, useEffect } from 'preact/hooks';
import { getStatus, setBrightness, getEffects, setEffect, setPower } from './utils';

import './styles.css';

const DEFAULT_BRIGHTNESS = 10;

const App = () => {
	const [value, setValue] = useState<number>(0);
	const [locked, setLocked] = useState(false);
	const lastSetVal = useRef(0);
	const sliderRef = useRef<HTMLDivElement>(null);
	const [isDragging, setIsDragging] = useState<boolean>(false);
	const [effects, setEffects] = useState<string[]>([]);
	const [currentEffect, setCurrentEffect] = useState<string>('');
	const [isOn, setIsOn] = useState<boolean>(true);

	// Обработчик переключения switcher
	const handleSwitcherToggle = async () => {
		const newState = !isOn;
		setIsOn(newState);
		await setPower(newState);
	};

	// Функция для расчета значения на основе позиции касания/клика
	const calculateValue = (clientY: number) => {
		if (!sliderRef.current) return;

		const rect = sliderRef.current.getBoundingClientRect();
		const height = rect.height;
		const y = clientY - rect.top; // Позиция относительно верхней части шкалы
		const newValue = Math.max(0, Math.min(100, ((height - y) / height) * 100)); // Переводим в процент
		setValue(newValue);
	};

	// Обработчики для мыши
	const handleMouseDown = (e: MouseEvent) => {
		setIsDragging(true);
		calculateValue(e.clientY);
	};

	const handleMouseMove = (e: MouseEvent) => {
		if (isDragging) {
			calculateValue(e.clientY);
		}
	};

	const handleMouseUp = () => {
		setIsDragging(false);
	};

	// Обработчики для сенсорных устройств
	const handleTouchStart = (e: TouchEvent) => {
		setIsDragging(true);
		calculateValue(e.touches[0].clientY);
	};

	const handleTouchMove = (e: TouchEvent) => {
		if (isDragging) {
			calculateValue(e.touches[0].clientY);
		}
	};

	const handleTouchEnd = () => {
		setIsDragging(false);
	};

	// Обработчик выбора эффекта
	const handleEffectSelect = async (effectName: string) => {
		setCurrentEffect(effectName);
		await setEffect(effectName);
	};

	useEffect(() => {
		const fetchInitialValue = async () => {
			const { brightness = "", current_effect = "", is_running = true } = await getStatus();
			const value = (+brightness || DEFAULT_BRIGHTNESS);
			setValue(value);
			setCurrentEffect(current_effect);
			setIsOn(is_running);

			// Загружаем список эффектов
			const effectsList = await getEffects();
			setEffects(effectsList || []);
		};

		fetchInitialValue();
	}, []);

	useEffect(() => {
		const brightness = Math.round(value); // Округляем значение яркости

		const sendBrightness = async () => {
			setLocked(true);
			setLocked(true);
			const { error } = await setBrightness(brightness);

			if (error) {
				throw new Error(`HTTP error! Status: ${error}`);
			}
			lastSetVal.current = brightness;
			setLocked(false);
		};

		if (!locked && lastSetVal.current !== brightness)
			sendBrightness();
	}, [value, locked]);

	// Добавляем глобальные обработчики для событий движения и отпускания
	useEffect(() => {
		window.addEventListener('mousemove', handleMouseMove);
		window.addEventListener('mouseup', handleMouseUp);
		window.addEventListener('touchmove', handleTouchMove);
		window.addEventListener('touchend', handleTouchEnd);

		return () => {
			window.removeEventListener('mousemove', handleMouseMove);
			window.removeEventListener('mouseup', handleMouseUp);
			window.removeEventListener('touchmove', handleTouchMove);
			window.removeEventListener('touchend', handleTouchEnd);
		};
	}, [isDragging]);

	// Генерируем 10 полосок-делений
	const markers = Array.from({ length: 10 }, (_, index) => (
		<div
			key={index}
			className="marker"
			style={{ bottom: `${(index + 1) * 10}%` }} // Распределяем полоски равномерно (10%, 20%, ..., 90%)
		/>
	));
	return (
		<div className="app-container">
			<div
				className="slider-container"
				ref={sliderRef}
				onMouseDown={handleMouseDown}
				onTouchStart={handleTouchStart}
			>
				{markers}
				<div
					className="slider-fill"
					style={{ height: `${value}%` }}
				/>
				<div className="brightness-label">
					{Math.round(value)}%
				</div>
			</div>

			{/* Горизонтальный скроллируемый контрол для эффектов */}
			<div className="effects-container">
				<div className="effects-scroll">
					{/* Switcher компонент */}
					<div
						className={`switcher ${isOn ? 'active' : ''}`}
						onClick={handleSwitcherToggle}
					>
						<div className="switcher-handle">
							{isOn ? 'ON' : 'OFF'}
						</div>
					</div>

					{effects.map((effect) => (
						<div
							key={effect}
							className={`effect-item ${currentEffect === effect ? 'active' : ''}`}
							onClick={() => handleEffectSelect(effect)}
						>
							{effect}
						</div>
					))}
				</div>
			</div>
		</div>
	);
};

render(<App />, document.getElementById('root') as HTMLElement);
