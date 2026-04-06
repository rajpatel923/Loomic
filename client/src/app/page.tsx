import LoginExperience from "@/components/LoginExperience";

export default function Home() {
  return (
    <main className="flex flex-1 items-center justify-center px-4 py-8 sm:px-6">
      <div className="w-full max-w-md">
        <div className="mb-6 text-center">
          <p className="eyebrow mb-2 text-[10px] text-[var(--accent)]">
            Welcome Back
          </p>
          <h1 className="font-display text-5xl leading-none tracking-[-0.06em] text-[var(--foreground)] sm:text-6xl">
            Loomic
          </h1>
        </div>
        <LoginExperience />
      </div>
    </main>
  );
}
