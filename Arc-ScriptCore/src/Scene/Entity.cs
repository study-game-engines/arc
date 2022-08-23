namespace ArcEngine
{
	public class Entity
	{
		protected Entity()
		{
			ID = 0;
		}

		private Entity(ulong id)
		{
			ID = id;
			Log.Info("Setting ID: {0}", id);
		}

		internal ulong ID { get; private set; }

		public bool HasComponent<T>() where T : Component, new() => InternalCalls.Entity_HasComponent(ID, typeof(T));

		public T AddComponent<T>() where T : Component, new()
		{
			InternalCalls.Entity_AddComponent(ID, typeof(T));
			T component = new T();
			component.Entity = this;
			return component;
		}

		public T GetComponent<T>() where T : Component, new()
		{
			if (HasComponent<T>())
			{
				T component = new T();
				component.Entity = this;
				return component;
			}

			return null;
		}
	}
}
